#pragma once
#include <boost/beast/websocket.hpp>
#include <memory>
#include "matching_engine.h"

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::ip::tcp::socket socket, matching::MatchingEngine& engine);
    void start();

private:
    void do_read();
    void handle_message();

    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_;
    boost::beast::flat_buffer buffer_;
    matching::MatchingEngine& engine_;
};
