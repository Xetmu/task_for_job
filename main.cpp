#include "server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>

AsyncServer* server = nullptr;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (server) {
        server->stop();
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port number: " << argv[1] << std::endl;
            return 1;
        }
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    AsyncServer srv(port);
    server = &srv;
    
    if (!srv.initialize()) {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }
    
    srv.run();
    
    return 0;
}
