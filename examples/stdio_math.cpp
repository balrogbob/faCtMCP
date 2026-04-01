#include "factmcp.h"

#include <cstdlib>
#include <string>

namespace {

const char* kAddToolJson =
    "{"
    "\"name\":\"add\"," 
    "\"description\":\"Add two integers\"," 
    "\"inputSchema\":{"
        "\"type\":\"object\"," 
        "\"properties\":{"
            "\"a\":{\"type\":\"number\"},"
            "\"b\":{\"type\":\"number\"}"
        "},"
        "\"required\":[\"a\",\"b\"]"
    "}"
    "}";

int extract_number(const std::string& params, const std::string& key) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = params.find(needle);
    if (pos == std::string::npos) {
        return 0;
    }
    pos = params.find(':', pos);
    if (pos == std::string::npos) {
        return 0;
    }
    ++pos;
    while (pos < params.size() && (params[pos] == ' ' || params[pos] == '\t')) {
        ++pos;
    }
    return std::atoi(params.c_str() + pos);
}

const char* add_handler(const char* json_params, void*) {
    static std::string result;
    const std::string params = json_params ? json_params : "{}";
    const int a = extract_number(params, "a");
    const int b = extract_number(params, "b");
    result = std::string("{\"success\":true,\"sum\":") + std::to_string(a + b) + "}";
    return result.c_str();
}

} // namespace

int main() {
    factmcp_server* server = factmcp_create(FACTMCP_TRANSPORT_STDIO, 0);
    if (!server) {
        return 1;
    }

    if (!factmcp_register_tool(server, "add", kAddToolJson, add_handler, nullptr)) {
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
