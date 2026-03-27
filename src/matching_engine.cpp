#include "matching_engine.h"
#include <algorithm>
#include <sstream>

namespace matching {

MatchingEngine::MatchingEngine() = default;

MatchingEngine::~MatchingEngine() {
    stop();
}

void MatchingEngine::start() {
    running_ = true;
    worker_ = std::thread(&MatchingEngine::processCommands, this);
}

void MatchingEngine::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void MatchingEngine::submitCommand(Command cmd) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        command_queue_.push(std::move(cmd));
    }
    cv_.notify_one();
}

void MatchingEngine::processCommands() {
    while (true) {
        Command cmd;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !command_queue_.empty() || !running_; });
            if (!running_ && command_queue_.empty()) break;
            cmd = std::move(command_queue_.front());
            command_queue_.pop();
        }

        // Process command with engine state locked
        std::lock_guard<std::mutex> engine_lock(mutex_);
        try {
            switch (cmd.type) {
                case CommandType::NEW:    handleNew(cmd); break;
                case CommandType::CANCEL: handleCancel(cmd); break;
                case CommandType::MODIFY: handleModify(cmd); break;
                case CommandType::PRINT:  handlePrint(cmd); break;
            }
        } catch (const std::exception& e) {
            std::string err = R"({"error":")" + std::string(e.what()) + R"("})";
            cmd.response_callback(err);
        }
    }
}

void MatchingEngine::handleNew(const Command& cmd) {
    if (orders_.count(cmd.id)) {
        cmd.response_callback(R"({"error":"Order ID already exists"})");
        return;
    }
    Order order{cmd.id, cmd.client_id, cmd.side, cmd.price, cmd.quantity, cmd.timestamp};
    if (order.side == Side::BUY) {
        matchBuy(order);
        if (order.quantity > 0) addToBook(order);
    } else {
        matchSell(order);
        if (order.quantity > 0) addToBook(order);
    }
    orders_[order.id] = std::move(order);
    cmd.response_callback(R"({"status":"accepted","order_id":)" + std::to_string(cmd.id) + "}");
}

void MatchingEngine::handleCancel(const Command& cmd) {
    auto it = orders_.find(cmd.id);
    if (it == orders_.end()) {
        cmd.response_callback(R"({"error":"Order not found"})");
        return;
    }
    removeFromBook(cmd.id);
    orders_.erase(it);
    cmd.response_callback(R"({"status":"cancelled","order_id":)" + std::to_string(cmd.id) + "}");
}

void MatchingEngine::handleModify(const Command& cmd) {
    auto it = orders_.find(cmd.id);
    if (it == orders_.end()) {
        cmd.response_callback(R"({"error":"Order not found"})");
        return;
    }
    // Cancel old order
    removeFromBook(cmd.id);
    orders_.erase(it);
    // Create new order with same ID but updated fields
    Order new_order{cmd.id, cmd.client_id, cmd.side, cmd.price, cmd.quantity, cmd.timestamp};
    if (new_order.side == Side::BUY) {
        matchBuy(new_order);
        if (new_order.quantity > 0) addToBook(new_order);
    } else {
        matchSell(new_order);
        if (new_order.quantity > 0) addToBook(new_order);
    }
    orders_[new_order.id] = std::move(new_order);
    cmd.response_callback(R"({"status":"modified","order_id":)" + std::to_string(cmd.id) + "}");
}

void MatchingEngine::handlePrint(const Command& cmd) {
    std::ostringstream oss;
    oss << R"({"type":"book","buys":[)";
    for (auto it = buy_book_.begin(); it != buy_book_.end(); ++it) {
        oss << "{\"price\":" << it->first << ",\"qty\":";
        int total = 0;
        for (const auto& key : it->second) {
            auto oit = orders_.find(key.id);
            if (oit != orders_.end()) total += oit->second.quantity;
        }
        oss << total << "}";
        if (std::next(it) != buy_book_.end()) oss << ",";
    }
    oss << R"(],"sells":[)";
    for (auto it = sell_book_.begin(); it != sell_book_.end(); ++it) {
        oss << "{\"price\":" << it->first << ",\"qty\":";
        int total = 0;
        for (const auto& key : it->second) {
            auto oit = orders_.find(key.id);
            if (oit != orders_.end()) total += oit->second.quantity;
        }
        oss << total << "}";
        if (std::next(it) != sell_book_.end()) oss << ",";
    }
    oss << R"(],"clients":[)";
    for (const auto& [cid, info] : clients_) {
        long long net_pos = info.bought_qty - info.sold_qty;
        long long pnl = info.sold_value - info.bought_value;
        oss << "{\"id\":" << cid << ",\"net_position\":" << net_pos
            << ",\"pnl\":" << pnl << "},";
    }
    std::string out = oss.str();
    if (out.back() == ',') out.pop_back();
    out += "]}";
    cmd.response_callback(out);
}

void MatchingEngine::matchBuy(Order& buy) {
    auto it = sell_book_.begin();
    while (it != sell_book_.end() && it->first <= buy.price && buy.quantity > 0) {
        auto& level = it->second;
        auto key_it = level.begin();
        while (key_it != level.end() && buy.quantity > 0) {
            auto sell_it = orders_.find(key_it->id);
            if (sell_it == orders_.end()) {
                key_it = level.erase(key_it);
                continue;
            }
            Order& sell = sell_it->second;
            int fill_qty = std::min(buy.quantity, sell.quantity);
            buy.quantity -= fill_qty;
            sell.quantity -= fill_qty;
            updateClient(buy, fill_qty, sell.price);
            updateClient(sell, fill_qty, sell.price);
            if (sell.quantity == 0) {
                orders_.erase(sell_it);
                key_it = level.erase(key_it);
            } else {
                ++key_it;
            }
        }
        if (level.empty()) it = sell_book_.erase(it);
        else ++it;
    }
}

void MatchingEngine::matchSell(Order& sell) {
    auto it = buy_book_.rbegin(); // highest price first
    while (it != buy_book_.rend() && it->first >= sell.price && sell.quantity > 0) {
        auto& level = it->second;
        auto key_it = level.begin();
        while (key_it != level.end() && sell.quantity > 0) {
            auto buy_it = orders_.find(key_it->id);
            if (buy_it == orders_.end()) {
                key_it = level.erase(key_it);
                continue;
            }
            Order& buy = buy_it->second;
            int fill_qty = std::min(sell.quantity, buy.quantity);
            sell.quantity -= fill_qty;
            buy.quantity -= fill_qty;
            updateClient(sell, fill_qty, buy.price);
            updateClient(buy, fill_qty, buy.price);
            if (buy.quantity == 0) {
                orders_.erase(buy_it);
                key_it = level.erase(key_it);
            } else {
                ++key_it;
            }
        }
        if (level.empty()) {
            // erase the price level (need to convert reverse iterator)
            auto forward_it = std::next(it).base();
            buy_book_.erase(forward_it);
            it = buy_book_.rbegin();
        } else {
            ++it;
        }
    }
}

void MatchingEngine::addToBook(const Order& order) {
    OrderTimeKey key{order.timestamp, order.id};
    if (order.side == Side::BUY) {
        buy_book_[order.price].insert(key);
    } else {
        sell_book_[order.price].insert(key);
    }
}

void MatchingEngine::removeFromBook(int order_id) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) return;
    const Order& order = it->second;
    OrderTimeKey key{order.timestamp, order.id};
    if (order.side == Side::BUY) {
        auto level_it = buy_book_.find(order.price);
        if (level_it != buy_book_.end()) {
            level_it->second.erase(key);
            if (level_it->second.empty()) buy_book_.erase(level_it);
        }
    } else {
        auto level_it = sell_book_.find(order.price);
        if (level_it != sell_book_.end()) {
            level_it->second.erase(key);
            if (level_it->second.empty()) sell_book_.erase(level_it);
        }
    }
}

void MatchingEngine::updateClient(const Order& order, int filled_qty, int trade_price) {
    ClientInfo& info = clients_[order.client_id];
    if (order.side == Side::BUY) {
        info.bought_qty += filled_qty;
        info.bought_value += static_cast<long long>(filled_qty) * trade_price;
    } else {
        info.sold_qty += filled_qty;
        info.sold_value += static_cast<long long>(filled_qty) * trade_price;
    }
}

} // namespace matching
