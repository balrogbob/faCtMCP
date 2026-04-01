#include "factmcp.h"

#include "mcp/FastMCPServer.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct factmcp_tool_callback {
    factmcp_tool_handler handler;
    void* user_data;
};

struct factmcp_server {
    explicit factmcp_server(FastMCPServer::Transport transport, int port)
        : server(transport, port) {}

    FastMCPServer server;
    std::vector<std::unique_ptr<factmcp_tool_callback>> callbacks;
    std::string last_result;
};

namespace {

FastMCPServer::Transport to_cpp_transport(factmcp_transport transport) {
    return transport == FACTMCP_TRANSPORT_STDIO
        ? FastMCPServer::Transport::STDIO
        : FastMCPServer::Transport::HTTP;
}

const char* duplicate_c_string(const std::string& value) {
    char* result = static_cast<char*>(std::malloc(value.size() + 1));
    if (!result) {
        return nullptr;
    }
    std::memcpy(result, value.c_str(), value.size() + 1);
    return result;
}

} // namespace

extern "C" {

factmcp_server* factmcp_create(factmcp_transport transport, int port) {
    return new factmcp_server(to_cpp_transport(transport), port);
}

void factmcp_destroy(factmcp_server* server) {
    delete server;
}

int factmcp_start(factmcp_server* server) {
    if (!server) {
        return 0;
    }
    return server->server.start() ? 1 : 0;
}

void factmcp_stop(factmcp_server* server) {
    if (!server) {
        return;
    }
    server->server.stop();
}

void factmcp_run(factmcp_server* server) {
    if (!server) {
        return;
    }
    server->server.run();
}

int factmcp_port(const factmcp_server* server) {
    if (!server) {
        return 0;
    }
    return server->server.port();
}

int factmcp_register_tool(
    factmcp_server* server,
    const char* name,
    const char* tool_json,
    factmcp_tool_handler handler,
    void* user_data) {
    if (!server || !name || !tool_json || !handler) {
        return 0;
    }

    auto callback = std::make_unique<factmcp_tool_callback>();
    callback->handler = handler;
    callback->user_data = user_data;
    factmcp_tool_callback* raw_callback = callback.get();
    server->callbacks.push_back(std::move(callback));

    server->server.register_command(name, tool_json, [raw_callback](const std::string& params) {
        const char* response = raw_callback->handler(params.c_str(), raw_callback->user_data);
        return response ? std::string(response) : std::string("{\"success\":false,\"error\":\"Tool handler returned null\"}");
    });
    return 1;
}

const char* factmcp_process_jsonrpc(factmcp_server* server, const char* json_body) {
    if (!server || !json_body) {
        return nullptr;
    }
    server->last_result = server->server.ProcessJsonRpc(json_body);
    return duplicate_c_string(server->last_result);
}

void factmcp_free_string(const char* text) {
    std::free(const_cast<char*>(text));
}

} // extern "C"
