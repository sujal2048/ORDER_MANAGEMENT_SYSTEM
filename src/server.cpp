#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <iostream>
#include <memory>
#include "matching_engine.h"
#include "session.h"

class WebSocketServer {
public:
    WebSocketServer(boost::asio::io_context& ioc, int port, matching::MatchingEngine& engine)
        : acceptor_(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
          engine_(engine) {}

    void run() {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
                if (!ec) {
                    std::make_shared<Session>(std::move(socket), engine_)->start();
                }
                do_accept();
            });
    }

    boost::asio::ip::tcp::acceptor acceptor_;
    matching::MatchingEngine& engine_;
};

int main(int argc, char* argv[]) {
    int port = 9000;
    if (argc >= 2) port = std::stoi(argv[1]);

    boost::asio::io_context ioc{1};
    matching::MatchingEngine engine;
    engine.start();

    WebSocketServer server(ioc, port, engine);
    server.run();

    std::cout << "Order matching engine listening on ws://localhost:" << port << std::endl;
    ioc.run();

    engine.stop();
    return 0;
}
