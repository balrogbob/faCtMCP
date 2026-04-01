#include "example_server_utils.h"

#include <string>

int run_example_stdio_server(const ExampleToolRegistration* registrations, int count) {
    if (!registrations || count <= 0) {
        return 1;
    }

    factmcp_server* server = factmcp_create(FACTMCP_TRANSPORT_STDIO, 0);
    if (!server) {
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        if (!factmcp_register_tool(
                server,
                registrations[i].name,
                registrations[i].tool_json,
                registrations[i].handler,
                registrations[i].user_data)) {
            factmcp_destroy(server);
            return 1;
        }
    }

    if (!factmcp_start(server)) {
        factmcp_destroy(server);
        return 1;
    }

    factmcp_run(server);
    factmcp_destroy(server);
    return 0;
}

std::string example_json_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}
