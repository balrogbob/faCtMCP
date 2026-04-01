#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
typedef int SOCKET;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#endif

class FastMCPServer {
public:
    enum class Transport { HTTP, STDIO };

    struct HandlerData {
        std::string name;
        std::string tool_json;
        std::function<std::string(const std::string&)> handler;
    };

    explicit FastMCPServer(Transport t = Transport::HTTP, int port = 8080);
    ~FastMCPServer();

    void stop();
    bool start();
    void run();

    void register_command(const std::string& name,
                          const std::string& tool_json,
                          const std::function<std::string(const std::string& params)>& handler);

    std::string ProcessJsonRpc(const std::string& body);
    int port() const;
    Transport transport() const;

private:
    Transport transport_;
    int port_;
    std::atomic<bool> running_{false};
    std::vector<std::thread> worker_threads_;
    std::vector<HandlerData> handlers_;

    SOCKET server_socket_ = INVALID_SOCKET;

    bool init_socket_runtime();
    void cleanup_socket_runtime();
    void handle_client(SOCKET client_socket);
    std::string parse_http_body(const std::string& http_request);
    std::string parse_http_path(const std::string& http_request);
    std::string build_http_response(const std::string& body, int status_code = 200);
    std::string build_sse_response(const std::string& body);
    std::string dispatch_command(const std::string& json_body);
    std::string handle_jsonrpc(const std::string& body);
    std::string append_injection_context(const std::string& result);
    void handle_sse_client(SOCKET client_socket);
    void run_stdio();
};
