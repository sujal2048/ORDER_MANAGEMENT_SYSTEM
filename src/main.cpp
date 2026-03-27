#include "matching_engine.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <iostream>
#include <memory>

// WebSocket server implementation (simplified for brevity – full version in actual codebase)
// This is a placeholder; the real server uses Boost.Beast and calls engine.submitCommand().

int main(int argc, char* argv[]) {
    try {
        // Parse port (default 9000)
        int port = 9000;
        if (argc >= 2) port = std::stoi(argv[1]);

        boost::asio::io_context ioc{1};
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) { ioc.stop(); });

        matching::MatchingEngine engine;
        engine.start();

        // Start WebSocket server on ioc (not shown – see full implementation)
        // ...

        std::cout << "Order matching engine listening on ws://localhost:" << port << std::endl;
        ioc.run();

        engine.stop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
