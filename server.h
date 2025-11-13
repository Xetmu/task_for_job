#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <set>
#include <netinet/in.h>

class AsyncServer {
public:
    AsyncServer(int port = 8080);
    ~AsyncServer();
    
    bool initialize();
    void run();
    void stop();

private:
    int create_tcp_socket();
    int create_udp_socket();
    bool setup_epoll();
    void handle_tcp_connection();
    void handle_udp_data();
    void handle_client_data(int client_fd);
    void process_command(int client_fd, const std::string& command);
    void send_response(int client_fd, const std::string& response);
    void close_client(int client_fd);
    std::string get_current_time();
    std::string get_stats();
    void add_udp_client(const struct sockaddr_in& client_addr);
    
    int port_;
    int tcp_fd_;
    int udp_fd_;
    int epoll_fd_;
    bool running_;
    
    std::atomic<int> total_tcp_clients_;
    std::atomic<int> current_tcp_clients_;
    std::atomic<int> total_udp_clients_;
    std::atomic<int> current_udp_clients_;
    
    std::unordered_map<int, std::string> client_buffers_;
    std::mutex udp_clients_mutex_;
    std::set<std::string> current_udp_clients_set_;
};

#endif