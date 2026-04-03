#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
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
    struct SSEClient {
        SOCKET socket;
        bool active;
        uint64_t last_event_id;
        bool legacy_sse_mode;
    };

    Transport transport_;
    int port_;
    std::atomic<bool> running_{false};
    std::vector<std::thread> worker_threads_;
    std::vector<HandlerData> handlers_;

    SOCKET server_socket_ = INVALID_SOCKET;

    std::string session_id_;
    std::mutex sse_clients_mutex_;
    std::vector<SSEClient> sse_clients_;
    std::atomic<uint64_t> next_event_id_{0};

    std::string server_name_ = "faCtMCP";
    std::string server_version_ = "0.1.0";
    std::string protocol_version_ = "2025-06-18";
    std::string active_protocol_version_ = "2025-06-18";

    bool init_socket_runtime();
    void cleanup_socket_runtime();
    void handle_client(SOCKET client_socket);
    std::string parse_http_body(const std::string& http_request);
    std::string parse_http_path(const std::string& http_request);
    std::string parse_http_method(const std::string& http_request);
    std::string extract_header(const std::string& request, const std::string& header_name);
    std::string build_http_response(const std::string& body, int status_code = 200, const std::map<std::string, std::string>& extra_headers = {});
    std::string build_sse_response(const std::string& body);
    std::string dispatch_command(const std::string& json_body);
    std::string handle_jsonrpc(const std::string& body);
    std::string append_injection_context(const std::string& result);
    void handle_sse_client(SOCKET client_socket);
    void handle_sse_stream(SOCKET client_socket, const std::string& last_event_id);
    void handle_post_messages(SOCKET client_socket, const std::string& body, const std::string& session_header, const std::string& protocol_version_header, bool legacy_sse_mode = false);
    std::string handle_post_messages_jsonrpc(const std::string& body, const std::string& session_header, const std::string& protocol_version_header, bool* out_is_notification);
    void broadcast_sse_event(const std::string& event, const std::string& data, uint64_t event_id);
    std::string generate_session_id();
    bool validate_session(const std::string& session_header);
    void remove_sse_client(SOCKET socket);
    void run_stdio();
};
