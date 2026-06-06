#define _CRT_SECURE_NO_WARNINGS 1  // <-- ADD THIS LINE FIRST

#include <iostream>
#include <string>
#include <unordered_map>
#include <cstring>

// Switch headers based on Operating System automatically
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef int socklen_t;
#define close_socket closesocket
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define close_socket close
#endif

std::unordered_map<std::string, std::string> fee_database = {
    {"STU001", "Paid"},
    {"STU002", "Pending"},
    {"STU003", "Paid"}
};

void handle_client(int client_socket) {
    char buffer[1024] = { 0 };
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        close_socket(client_socket);
        return;
    }

    std::string request(buffer);
    std::string route = "/get-fee-status/";
    size_t route_pos = request.find(route);

    std::string response_body;
    std::string http_status = "200 OK";

    if (route_pos != std::string::npos) {
        size_t id_start = route_pos + route.length();
        size_t id_end = request.find(" ", id_start);
        std::string student_id = request.substr(id_start, id_end - id_start);

        if (fee_database.find(student_id) != fee_database.end()) {
            response_body = "{\"student_id\":\"" + student_id + "\",\"fee_status\":\"" + fee_database[student_id] + "\"}";
        }
        else {
            http_status = "404 Not Found";
            response_body = "{\"error\":\"Student Not Found\"}";
        }
    }
    else {
        http_status = "400 Bad Request";
        response_body = "{\"error\":\"Invalid Endpoint\"}";
    }

    std::string http_response =
        "HTTP/1.1 " + http_status + "\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
        "Connection: close\r\n\r\n" + response_body;

    send(client_socket, http_response.c_str(), http_response.length(), 0);
    close_socket(client_socket);
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Winsock initialization failed.\n";
        return 1;
    }
#endif

    char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 8080;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Fixed compatibility format for setsockopt across Windows/Linux architectures
#ifdef _WIN32
    char opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#else
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(server_fd, (sockaddr*)&address, sizeof(address));
    listen(server_fd, SOMAXCONN);

    std::cout << "Cross-platform C++ Server listening on port " << port << std::endl;

    while (true) {
        int client_socket = accept(server_fd, nullptr, nullptr);
        if (client_socket >= 0) {
            handle_client(client_socket);
        }
    }

    close_socket(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}