#include "server.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctime>
#include <sstream>
#include <algorithm>

#define MAX_EVENTS 64
#define BUFFER_SIZE 4096

AsyncServer::AsyncServer(int port) 
    : port_(port), tcp_fd_(-1), udp_fd_(-1), epoll_fd_(-1), 
      running_(false), total_tcp_clients_(0), current_tcp_clients_(0),
      total_udp_clients_(0), current_udp_clients_(0) {
}

AsyncServer::~AsyncServer() {
    stop();
}

bool AsyncServer::initialize() {
    tcp_fd_ = create_tcp_socket();
    if (tcp_fd_ == -1) {
        return false;
    }
    
    udp_fd_ = create_udp_socket();
    if (udp_fd_ == -1) {
        close(tcp_fd_);
        return false;
    }
    
    if (!setup_epoll()) {
        close(tcp_fd_);
        close(udp_fd_);
        return false;
    }
    
    std::cout << "Server initialized on port " << port_ << std::endl;
    return true;
}

int AsyncServer::create_tcp_socket() {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd == -1) {
        std::cerr << "Failed to create TCP socket: " << strerror(errno) << std::endl;
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set TCP socket options: " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind TCP socket: " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }
    
    if (listen(fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on TCP socket: " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }
    
    return fd;
}

int AsyncServer::create_udp_socket() {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd == -1) {
        std::cerr << "Failed to create UDP socket: " << strerror(errno) << std::endl;
        return -1;
    }
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind UDP socket: " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }
    
    return fd;
}

bool AsyncServer::setup_epoll() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        std::cerr << "Failed to create epoll instance: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Add TCP socket to epoll
    struct epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = tcp_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, tcp_fd_, &event) < 0) {
        std::cerr << "Failed to add TCP socket to epoll: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Add UDP socket to epoll
    event.data.fd = udp_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, udp_fd_, &event) < 0) {
        std::cerr << "Failed to add UDP socket to epoll: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}

void AsyncServer::run() {
    running_ = true;
    struct epoll_event events[MAX_EVENTS];
    
    std::cout << "Server started. Waiting for connections..." << std::endl;
    
    while (running_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }
        
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == tcp_fd_) {
                handle_tcp_connection();
            } else if (events[i].data.fd == udp_fd_) {
                handle_udp_data();
            } else {
                handle_client_data(events[i].data.fd);
            }
        }
    }
}

void AsyncServer::handle_tcp_connection() {
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept4(tcp_fd_, (struct sockaddr*)&client_addr, &addr_len, SOCK_NONBLOCK);
    if (client_fd == -1) {
        std::cerr << "Failed to accept TCP connection: " << strerror(errno) << std::endl;
        return;
    }
    
    // Add client to epoll
    struct epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = client_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) < 0) {
        std::cerr << "Failed to add client to epoll: " << strerror(errno) << std::endl;
        close(client_fd);
        return;
    }
    
    total_tcp_clients_++;
    current_tcp_clients_++;
    client_buffers_[client_fd] = "";
    
    std::cout << "New TCP connection from " << inet_ntoa(client_addr.sin_addr) 
              << ":" << ntohs(client_addr.sin_port) 
              << " (fd: " << client_fd << ")" 
              << " [TCP Total: " << total_tcp_clients_ << ", Current: " << current_tcp_clients_ 
              << " | UDP Total: " << total_udp_clients_ << ", Current: " << current_udp_clients_ << "]" << std::endl;
}

void AsyncServer::add_udp_client(const struct sockaddr_in& client_addr) {
    std::string client_key = std::string(inet_ntoa(client_addr.sin_addr)) + ":" + 
                            std::to_string(ntohs(client_addr.sin_port));
    
    std::lock_guard<std::mutex> lock(udp_clients_mutex_);
    
    if (current_udp_clients_set_.find(client_key) == current_udp_clients_set_.end()) {
        // Новый UDP клиент
        total_udp_clients_++;
        current_udp_clients_++;
        current_udp_clients_set_.insert(client_key);
        
        std::cout << "New UDP client: " << client_key 
                  << " [TCP Total: " << total_tcp_clients_ << ", Current: " << current_tcp_clients_ 
                  << " | UDP Total: " << total_udp_clients_ << ", Current: " << current_udp_clients_ << "]" << std::endl;
    }
}

void AsyncServer::handle_udp_data() {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    ssize_t len = recvfrom(udp_fd_, buffer, BUFFER_SIZE - 1, 0, 
                          (struct sockaddr*)&client_addr, &addr_len);
    if (len > 0) {
        buffer[len] = '\0';
        std::string message(buffer, len);
        
        // Удаляем символы новой строки и возврата каретки
        message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());
        message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());
        
        // Добавляем UDP клиента в статистику
        add_udp_client(client_addr);
        
        std::cout << "UDP message from " << inet_ntoa(client_addr.sin_addr) 
                  << ":" << ntohs(client_addr.sin_port) << ": " << message << std::endl;
        
        std::string response;
        if (!message.empty() && message[0] == '/') {
            // Process command
            if (message == "/time") {
                response = get_current_time() + "\n";
            } else if (message == "/stats") {
                response = get_stats() + "\n";
            } else if (message == "/shutdown") {
                response = "Shutdown command not supported via UDP\n";
            } else {
                response = "Unknown command: " + message + "\nAvailable commands: /time, /stats\n";
            }
        } else {
            // Mirror message with newline
            response = message + "\n";
        }
        
        sendto(udp_fd_, response.c_str(), response.length(), 0,
               (struct sockaddr*)&client_addr, addr_len);
    }
}

void AsyncServer::handle_client_data(int client_fd) {
    char buffer[BUFFER_SIZE];
    
    while (true) {
        ssize_t len = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (len > 0) {
            buffer[len] = '\0';
            std::string message(buffer, len);
            
            // Удаляем символы новой строки и возврата каретки
            message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());
            message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());
            
            std::cout << "Received from client " << client_fd << ": " << message << std::endl;
            
            if (!message.empty() && message[0] == '/') {
                process_command(client_fd, message);
            } else {
                // Mirror the message back to client with newline
                send_response(client_fd, message + "\n");
            }
        } else if (len == 0) {
            // Connection closed
            std::cout << "Client " << client_fd << " disconnected" 
                      << " [TCP Current: " << (current_tcp_clients_ - 1) 
                      << " | UDP Current: " << current_udp_clients_ << "]" << std::endl;
            close_client(client_fd);
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                std::cerr << "Error reading from client " << client_fd << ": " << strerror(errno) << std::endl;
                close_client(client_fd);
                break;
            }
        }
    }
}

void AsyncServer::process_command(int client_fd, const std::string& command) {
    std::string response;
    
    if (command == "/time") {
        response = get_current_time() + "\n";
    } else if (command == "/stats") {
        response = get_stats() + "\n";
    } else if (command == "/shutdown") {
        response = "Server shutting down...\n";
        send_response(client_fd, response);
        stop();
        return;
    } else {
        response = "Unknown command: " + command + "\nAvailable commands: /time, /stats, /shutdown\n";
    }
    
    send_response(client_fd, response);
}

void AsyncServer::send_response(int client_fd, const std::string& response) {
    ssize_t sent = send(client_fd, response.c_str(), response.length(), 0);
    if (sent < 0) {
        std::cerr << "Failed to send response to client " << client_fd << ": " << strerror(errno) << std::endl;
    }
}

void AsyncServer::close_client(int client_fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    client_buffers_.erase(client_fd);
    current_tcp_clients_--;
}

std::string AsyncServer::get_current_time() {
    time_t now = time(nullptr);
    tm* local_time = localtime(&now);
    
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_time);
    
    return std::string(buffer);
}

std::string AsyncServer::get_stats() {
    std::ostringstream ss;
    ss << "Total clients: " << (total_tcp_clients_ + total_udp_clients_)
       << " (TCP: " << total_tcp_clients_ << ", UDP: " << total_udp_clients_ << ")"
       << ", Current clients: " << (current_tcp_clients_ + current_udp_clients_)
       << " (TCP: " << current_tcp_clients_ << ", UDP: " << current_udp_clients_ << ")";
    return ss.str();
}

void AsyncServer::stop() {
    running_ = false;
    
    // Close all client connections
    for (auto& pair : client_buffers_) {
        close(pair.first);
    }
    client_buffers_.clear();
    
    // Clear UDP clients
    {
        std::lock_guard<std::mutex> lock(udp_clients_mutex_);
        current_udp_clients_set_.clear();
        current_udp_clients_ = 0;
    }
    
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if (tcp_fd_ != -1) {
        close(tcp_fd_);
        tcp_fd_ = -1;
    }
    if (udp_fd_ != -1) {
        close(udp_fd_);
        udp_fd_ = -1;
    }
    
    std::cout << "Server stopped" << std::endl;
}