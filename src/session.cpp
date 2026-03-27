#include "session.h"
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>
#include "matching_engine.h"

Session::Session(boost::asio::ip::tcp::socket socket, matching::MatchingEngine& engine)
    : ws_(std::move(socket)), engine_(engine) {}

void Session::start() {
    ws_.async_accept([self = shared_from_this()](boost::system::error_code ec) {
        if (!ec) self->do_read();
    });
}

void Session::do_read() {
    ws_.async_read(buffer_,
        [self = shared_from_this()](boost::system::error_code ec, std::size_t bytes) {
            if (ec) return;
            self->handle_message();
            self->do_read();
        });
}

void Session::handle_message() {
    std::string msg = boost::beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());

    try {
        auto json = nlohmann::json::parse(msg);
        matching::Command cmd;
        cmd.id = json["id"];
        cmd.client_id = json["client_id"];
        cmd.timestamp = json["timestamp"];
        std::string type = json["type"];
        if (type == "NEW") {
            cmd.type = matching::CommandType::NEW;
            cmd.side = (json["side"] == "BUY") ? matching::Side::BUY : matching::Side::SELL;
            cmd.price = json["price"];
            cmd.quantity = json["quantity"];
        } else if (type == "CANCEL") {
            cmd.type = matching::CommandType::CANCEL;
        } else if (type == "MODIFY") {
            cmd.type = matching::CommandType::MODIFY;
            cmd.price = json["price"];
            cmd.quantity = json["quantity"];
            cmd.side = (json["side"] == "BUY") ? matching::Side::BUY : matching::Side::SELL;
        } else if (type == "PRINT") {
            cmd.type = matching::CommandType::PRINT;
        } else {
            throw std::runtime_error("Unknown command type");
        }

        auto self = shared_from_this();
        cmd.response_callback = [self](const std::string& resp) {
            self->ws_.write(boost::asio::buffer(resp));
        };

        engine_.submitCommand(std::move(cmd));
    } catch (const std::exception& e) {
        ws_.write(boost::asio::buffer(std::string("{\"error\":\"") + e.what() + "\"}"));
    }
}
