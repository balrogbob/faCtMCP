#include "factmcp.h"

#include <string>

namespace {

const char* kEchoToolJson =
    "{"
    "\"name\":\"echo\"," 
    "\"description\":\"Echo text back to the caller\"," 
    "\"inputSchema\":{"
        "\"type\":\"object\"," 
        "\"properties\":{"
            "\"text\":{\"type\":\"string\",\"description\":\"Text to echo\"}"
        "},"
        "\"required\":[\"text\"]"
    "}"
    "}";

std::string json_escape(const std::string& value) {
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

std::string extract_text(const std::string& params) {
    const std::string needle = "\"text\"";
    size_t pos = params.find(needle);
    if (pos == std::string::npos) {
        return "";
    }
    pos = params.find(':', pos);
    if (pos == std::string::npos) {
        return "";
    }
    pos = params.find('"', pos);
    if (pos == std::string::npos) {
        return "";
    }
    size_t end = pos + 1;
    while (end < params.size()) {
        if (params[end] == '"' && params[end - 1] != '\\') {
            break;
        }
        ++end;
    }
    if (end >= params.size()) {
        return "";
    }
    return params.substr(pos + 1, end - pos - 1);
}

const char* echo_handler(const char* json_params, void*) {
    static std::string result;
    const std::string text = json_params ? extract_text(json_params) : "";
    result = std::string("{\"success\":true,\"echo\":\"") + json_escape(text) + "\"}";
    return result.c_str();
}

} // namespace

int main() {
    factmcp_server* server = factmcp_create(FACTMCP_TRANSPORT_STDIO, 0);
    if (!server) {
        return 1;
    }

    if (!factmcp_register_tool(server, "echo", kEchoToolJson, echo_handler, nullptr)) {
        factmcp_destroy(server);
        return 1;
    }

    if (!factmcp_start(server)) {
        factmcp_destroy(server);
        return 1;
    }

    factmcp_run(server);
    factmcp_destroy(server);
    return 0;
}
