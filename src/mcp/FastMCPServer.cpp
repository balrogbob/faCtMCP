#include "FastMCPServer.h"

#include "InjectionQueue.h"
#include "ToolState.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
static int close_socket_handle(SOCKET socket_handle) {
    return closesocket(socket_handle);
}

static int socket_last_error() {
    return WSAGetLastError();
}
#else
#include <arpa/inet.h>
#include <cerrno>
#include <unistd.h>

static int close_socket_handle(SOCKET socket_handle) {
    return close(socket_handle);
}

static int socket_last_error() {
    return errno;
}
#endif

namespace {

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += ' ';
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string json_string_field(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = body.find('"', pos);
    if (pos == std::string::npos) return "";
    size_t end = pos + 1;
    while (end < body.size()) {
        if (body[end] == '"' && body[end - 1] != '\\') break;
        ++end;
    }
    if (end >= body.size()) return "";
    return body.substr(pos + 1, end - pos - 1);
}

std::string json_number_field(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }
    size_t end = pos;
    while (end < body.size() && (std::isdigit(static_cast<unsigned char>(body[end])) || body[end] == '-')) {
        ++end;
    }
    return body.substr(pos, end - pos);
}

std::string json_method(const std::string& body) {
    return json_string_field(body, "method");
}

std::string json_id(const std::string& body) {
    std::string id = json_number_field(body, "id");
    if (!id.empty()) return id;
    return json_string_field(body, "id");
}

std::string tool_list_json(const std::vector<FastMCPServer::HandlerData>& handlers) {
    std::string json = "{\"tools\":[";
    bool first = true;
    for (const auto& h : handlers) {
        if (!ToolState::IsEnabled(h.name)) {
            continue;
        }
        if (!first) {
            json += ",";
        }
        json += h.tool_json;
        first = false;
    }
    json += "]}";
    return json;
}

bool handler_result_failed(const std::string& result) {
    return result.find("\"success\": false") != std::string::npos ||
           result.find("\"success\":false") != std::string::npos;
}

std::string jsonrpc_result(const std::string& id, const std::string& result) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + (id.empty() ? "null" : id) + ",\"result\":" + result + "}";
}

std::string jsonrpc_error(const std::string& id, int code, const std::string& message) {
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + (id.empty() ? "null" : id) +
           ",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":\"" + json_escape(message) + "\"}}";
}

std::string injection_suffix(const std::string& text) {
    if (text.empty()) {
        return "";
    }
    return std::string("\n\n--- Additional Context Included ---\n") + text;
}

std::string lowercase_ascii(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

bool read_stdio_message(std::istream& input, std::string* body) {
    if (!body) {
        return false;
    }

    std::string line;
    int content_length = -1;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        const std::string lower = lowercase_ascii(line);
        if (lower.rfind("content-length:", 0) == 0) {
            content_length = std::stoi(line.substr(15));
        }
    }

    if (content_length < 0) {
        return false;
    }

    body->assign(static_cast<size_t>(content_length), '\0');
    input.read(&(*body)[0], content_length);
    return static_cast<int>(input.gcount()) == content_length;
}

void write_stdio_message(std::ostream& output, const std::string& body) {
    output << "Content-Length: " << body.size() << "\r\n\r\n";
    output << body;
    output.flush();
}

std::string extract_tool_name(const std::string& body) {
    size_t pos = body.find("\"name\"");
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = body.find('"', pos);
    if (pos == std::string::npos) return "";
    size_t end = pos + 1;
    while (end < body.size()) {
        if (body[end] == '"' && body[end - 1] != '\\') break;
        ++end;
    }
    if (end >= body.size()) return "";
    return body.substr(pos + 1, end - pos - 1);
}

std::string extract_arguments_object(const std::string& body) {
    size_t pos = body.find("\"arguments\"");
    if (pos == std::string::npos) return "{}";
    pos = body.find('{', pos);
    if (pos == std::string::npos) return "{}";
    int depth = 0;
    bool in_string = false;
    for (size_t i = pos; i < body.size(); ++i) {
        char c = body[i];
        if (c == '"' && (i == 0 || body[i - 1] != '\\')) {
            in_string = !in_string;
        }
        if (in_string) continue;
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) return body.substr(pos, i - pos + 1);
        }
    }
    return "{}";
}

} // namespace

FastMCPServer::FastMCPServer(Transport t, int port)
    : transport_(t), port_(port) {}

FastMCPServer::~FastMCPServer() {
    stop();
}

std::string FastMCPServer::append_injection_context(const std::string& result) {
    return result + injection_suffix(InjectionQueue::ConsumeIfDue());
}

bool FastMCPServer::init_socket_runtime() {
#ifdef _WIN32
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#else
    return true;
#endif
}

void FastMCPServer::cleanup_socket_runtime() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool FastMCPServer::start() {
    if (running_) {
        return false;
    }

    if (transport_ == Transport::STDIO) {
        running_ = true;
        return true;
    }

    if (!init_socket_runtime()) {
        std::cerr << "Failed to initialize socket runtime\n";
        return false;
    }

    int try_port = port_;
    constexpr int kMaxAttempts = 10;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (server_socket_ != INVALID_SOCKET) {
            close_socket_handle(server_socket_);
        }

        server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_socket_ == INVALID_SOCKET) {
            std::cerr << "Failed to create socket: " << socket_last_error() << "\n";
            cleanup_socket_runtime();
            return false;
        }

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<unsigned short>(try_port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(server_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR) {
            port_ = try_port;
            break;
        }

        std::cerr << "Port " << try_port << " in use, trying " << (try_port + 1) << "...\n";
        ++try_port;
    }

    if (listen(server_socket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << socket_last_error() << "\n";
        close_socket_handle(server_socket_);
        server_socket_ = INVALID_SOCKET;
        cleanup_socket_runtime();
        return false;
    }

    running_ = true;
    std::cout << "faCtMCP HTTP server started on port " << port_ << "\n";
    return true;
}

void FastMCPServer::run() {
    if (!running_) {
        return;
    }

    if (transport_ == Transport::STDIO) {
        run_stdio();
        return;
    }

    while (running_) {
        SOCKET client = accept(server_socket_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (running_) {
                std::cerr << "Accept failed: " << socket_last_error() << "\n";
            }
            continue;
        }

        worker_threads_.emplace_back([this, client]() {
            handle_client(client);
            close_socket_handle(client);
        });
    }
}

void FastMCPServer::run_stdio() {
    while (running_) {
        std::string body;
        if (!read_stdio_message(std::cin, &body)) {
            break;
        }
        const std::string response = handle_jsonrpc(body);
        if (!response.empty()) {
            write_stdio_message(std::cout, response);
        }
    }
}

std::string FastMCPServer::parse_http_body(const std::string& http_request) {
    size_t header_end = http_request.find("\r\n\r\n");
    if (header_end == std::string::npos) return "";

    std::string headers = http_request.substr(0, header_end);
    std::string body = http_request.substr(header_end + 4);

    std::transform(headers.begin(), headers.end(), headers.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    auto content_len_pos = headers.find("content-length:");
    if (content_len_pos == std::string::npos) return body;

    size_t line_end = headers.find("\r\n", content_len_pos);
    std::string content_len_line = headers.substr(content_len_pos, line_end - content_len_pos);
    size_t colon = content_len_line.find(':');
    if (colon == std::string::npos) return body;

    int content_length = std::stoi(content_len_line.substr(colon + 1));
    if (static_cast<int>(body.size()) < content_length) return "";

    return body.substr(0, static_cast<size_t>(content_length));
}

std::string FastMCPServer::parse_http_path(const std::string& http_request) {
    size_t line_end = http_request.find("\r\n");
    if (line_end == std::string::npos) return "";
    std::string request_line = http_request.substr(0, line_end);
    size_t first_space = request_line.find(' ');
    if (first_space == std::string::npos) return "";
    size_t second_space = request_line.find(' ', first_space + 1);
    if (second_space == std::string::npos) return "";
    return request_line.substr(first_space + 1, second_space - first_space - 1);
}

std::string FastMCPServer::build_http_response(const std::string& body, int status_code) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << ' ';
    switch (status_code) {
        case 200: response << "OK"; break;
        case 400: response << "Bad Request"; break;
        case 404: response << "Not Found"; break;
        case 500: response << "Internal Server Error"; break;
        default: response << "Unknown"; break;
    }
    response << "\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;
    return response.str();
}

std::string FastMCPServer::build_sse_response(const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/event-stream\r\n";
    response << "Cache-Control: no-cache\r\n";
    response << "Connection: keep-alive\r\n";
    response << "Access-Control-Allow-Origin: *\r\n\r\n";
    response << body;
    return response.str();
}

std::string FastMCPServer::dispatch_command(const std::string& json_body) {
    if (handlers_.empty()) {
        return R"({"success": false, "error": "No handlers registered"})";
    }
    for (const auto& h : handlers_) {
        if (json_body.find("\"" + h.name + "\"") != std::string::npos ||
            json_body.find("\"" + h.name + "()\"") != std::string::npos) {
            try {
                return h.handler(json_body);
            } catch (const std::exception& e) {
                return std::string(R"({"success": false, "error": ")") + e.what() + "\"}";
            }
        }
    }
    return R"({"success": false, "error": "Unknown command"})";
}

std::string FastMCPServer::handle_jsonrpc(const std::string& body) {
    const std::string method = json_method(body);
    const std::string id = json_id(body);

    if (method == "initialize") {
        return jsonrpc_result(id, R"({"protocolVersion":"2025-06-18","capabilities":{"tools":{}},"serverInfo":{"name":"faCtMCP","version":"0.1.0"}})" );
    }

    if (method == "notifications/initialized") {
        return "";
    }

    if (method == "tools/list") {
        return jsonrpc_result(id, tool_list_json(handlers_));
    }

    if (method == "tools/call") {
        const std::string tool_name = extract_tool_name(body);
        const std::string args = extract_arguments_object(body);
        if (!ToolState::IsEnabled(tool_name)) {
            return jsonrpc_error(id, -32601, std::string("Tool disabled: ") + tool_name);
        }
        for (const auto& h : handlers_) {
            if (h.name == tool_name) {
                try {
                    const std::string result = append_injection_context(h.handler(args));
                    const bool is_error = handler_result_failed(result);
                    return jsonrpc_result(id, std::string("{\"content\":[{\"type\":\"text\",\"text\":\"") + json_escape(result) + "\"}],\"isError\":" + (is_error ? "true" : "false") + "}");
                } catch (const std::exception& e) {
                    return jsonrpc_error(id, -32000, e.what());
                }
            }
        }
        return jsonrpc_error(id, -32601, "Unknown tool");
    }

    return jsonrpc_error(id, -32601, "Method not found");
}

std::string FastMCPServer::ProcessJsonRpc(const std::string& body) {
    return handle_jsonrpc(body);
}

void FastMCPServer::handle_client(SOCKET client_socket) {
    std::string request;
    char buffer[8192];
    size_t expected_total = 0;
    size_t header_end_pos = std::string::npos;

    while (true) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            break;
        }
        request.append(buffer, static_cast<size_t>(bytes_received));

        if (header_end_pos == std::string::npos) {
            header_end_pos = request.find("\r\n\r\n");
            if (header_end_pos != std::string::npos) {
                std::string headers = request.substr(0, header_end_pos);
                std::string lower_headers = lowercase_ascii(headers);
                size_t content_len_pos = lower_headers.find("content-length:");
                if (content_len_pos != std::string::npos) {
                    size_t line_end = lower_headers.find("\r\n", content_len_pos);
                    std::string content_len_line = lower_headers.substr(content_len_pos, line_end - content_len_pos);
                    size_t colon = content_len_line.find(':');
                    if (colon != std::string::npos) {
                        expected_total = (header_end_pos + 4) + static_cast<size_t>(std::stoi(content_len_line.substr(colon + 1)));
                    }
                } else {
                    expected_total = header_end_pos + 4;
                }
            }
        }

        if (expected_total > 0 && request.size() >= expected_total) {
            break;
        }
    }

    if (request.empty()) {
        return;
    }

    const std::string path = parse_http_path(request);
    if (request.rfind("GET", 0) == 0 && path.rfind("/sse", 0) == 0) {
        handle_sse_client(client_socket);
        return;
    }

    if (request.rfind("GET", 0) == 0 && (path == "/" || path == "/mcp" || path.empty())) {
        const std::string response = build_http_response(R"({"ok":true,"transport":"mcp"})", 200);
        send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
        return;
    }

    if (request.rfind("GET", 0) == 0 && path == "/tools") {
        const std::string response = build_http_response(tool_list_json(handlers_), 200);
        send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
        return;
    }

    if (request.rfind("GET", 0) == 0) {
        const std::string response = build_http_response(R"({"error":"Unknown endpoint"})", 404);
        send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
        return;
    }

    if (request.rfind("POST", 0) != 0) {
        const std::string response = build_http_response(R"({"error":"Only POST supported"})", 400);
        send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
        return;
    }

    if (!(path == "/" || path == "/mcp" || path == "/messages" || path.empty())) {
        const std::string response = build_http_response(R"({"error":"Unknown endpoint"})", 404);
        send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
        return;
    }

    const std::string body = parse_http_body(request);
    const std::string response_body = handle_jsonrpc(body);
    const std::string http_response = build_http_response(response_body);
    send(client_socket, http_response.c_str(), static_cast<int>(http_response.size()), 0);
}

void FastMCPServer::handle_sse_client(SOCKET client_socket) {
    const std::string response = build_sse_response("event: endpoint\ndata: /messages\n\n");
    send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
}

void FastMCPServer::register_command(const std::string& name,
                                     const std::string& tool_json,
                                     const std::function<std::string(const std::string&)>& handler) {
    handlers_.push_back({name, tool_json, handler});
}

void FastMCPServer::stop() {
    if (!running_) {
        return;
    }
    running_ = false;

    if (transport_ == Transport::HTTP) {
        if (server_socket_ != INVALID_SOCKET) {
            close_socket_handle(server_socket_);
            server_socket_ = INVALID_SOCKET;
        }
        for (auto& worker : worker_threads_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        worker_threads_.clear();
        cleanup_socket_runtime();
    }
}

int FastMCPServer::port() const {
    return port_;
}

FastMCPServer::Transport FastMCPServer::transport() const {
    return transport_;
}
