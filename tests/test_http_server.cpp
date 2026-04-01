#include "mcp/FastMCPServer.h"

#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

const char* kEchoToolJson = R"JSON({"name":"echo","description":"Echo text back to caller","inputSchema":{"type":"object","properties":{"text":{"type":"string"}},"required":["text"]}})JSON";

std::string echo_handler(const std::string& params) {
    return std::string("{\"success\":true,\"params\":") + params + "}";
}

std::string http_request(const std::string& host, int port, const std::string& request_text) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return "";
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return "";
    }

    send(sock, request_text.c_str(), static_cast<int>(request_text.size()), 0);

    std::string response;
    char buffer[4096];
    int received = 0;
    while ((received = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, buffer + received);
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return response;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    FastMCPServer server(FastMCPServer::Transport::HTTP, 18080);
    server.register_command("echo", kEchoToolJson, echo_handler);
    if (!server.start()) {
        std::cerr << "Failed to start HTTP test server\n";
        return 1;
    }

    std::thread server_thread([&server]() { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const int port = server.port();
    const std::string get_root = http_request("127.0.0.1", port, "GET /mcp HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    const std::string get_tools = http_request("127.0.0.1", port, "GET /tools HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    const std::string get_sse = http_request("127.0.0.1", port, "GET /sse HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    const std::string body = R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})";
    std::ostringstream post;
    post << "POST /mcp HTTP/1.1\r\n"
         << "Host: localhost\r\n"
         << "Content-Type: application/json\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n\r\n"
         << body;
    const std::string post_tools = http_request("127.0.0.1", port, post.str());

    const std::string bad_get = http_request("127.0.0.1", port, "GET /bad HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    if (!contains(get_root, "200 OK") || !contains(get_root, "transport")) {
        std::cerr << "GET /mcp failed\n" << get_root << "\n";
        return 2;
    }
    if (!contains(get_tools, "200 OK") || !contains(get_tools, "echo")) {
        std::cerr << "GET /tools failed\n" << get_tools << "\n";
        return 3;
    }
    if (!contains(get_sse, "text/event-stream") || !contains(get_sse, "/messages")) {
        std::cerr << "GET /sse failed\n" << get_sse << "\n";
        return 4;
    }
    if (!contains(post_tools, "200 OK") || !contains(post_tools, "tools")) {
        std::cerr << "POST /mcp failed\n" << post_tools << "\n";
        return 5;
    }
    if (!contains(bad_get, "404")) {
        std::cerr << "GET /bad failed\n" << bad_get << "\n";
        return 6;
    }

    return 0;
}
