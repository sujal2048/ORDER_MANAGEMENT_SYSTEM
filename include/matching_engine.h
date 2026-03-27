#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <condition_variable>   // <-- added

namespace matching {

enum class Side { BUY, SELL };
enum class CommandType { NEW, CANCEL, MODIFY, PRINT };

struct Order {
    int id;
    int client_id;
    Side side;
    int price;
    int quantity;          // remaining quantity
    long long timestamp;
};

struct OrderTimeKey {
    long long ts;
    int id;
    bool operator<(const OrderTimeKey& other) const {
        if (ts != other.ts) return ts < other.ts;
        return id < other.id;
    }
};

struct ClientInfo {
    long long bought_qty = 0;
    long long sold_qty = 0;
    long long bought_value = 0;
    long long sold_value = 0;
};

struct Command {
    CommandType type;
    int id;
    int client_id;
    Side side;            // for NEW
    int price;            // for NEW/MODIFY
    int quantity;         // for NEW/MODIFY
    long long timestamp;
    std::function<void(const std::string&)> response_callback;
};

class MatchingEngine {
public:
    MatchingEngine();
    ~MatchingEngine();

    void start();
    void stop();
    void submitCommand(Command cmd);

private:
    void processCommands();
    void handleNew(const Command& cmd);
    void handleCancel(const Command& cmd);
    void handleModify(const Command& cmd);
    void handlePrint(const Command& cmd);
    void matchBuy(Order& buy);
    void matchSell(Order& sell);
    void addToBook(const Order& order);
    void removeFromBook(int order_id);
    void updateClient(const Order& order, int filled_qty, int trade_price);

    std::unordered_map<int, Order> orders_;
    std::map<int, std::set<OrderTimeKey>, std::greater<int>> buy_book_;  // price descending
    std::map<int, std::set<OrderTimeKey>> sell_book_;                   // price ascending
    std::unordered_map<int, ClientInfo> clients_;

    std::mutex mutex_;
    std::queue<Command> command_queue_;
    std::condition_variable cv_;
    bool running_ = false;
    std::thread worker_;
};

} // namespace matching
