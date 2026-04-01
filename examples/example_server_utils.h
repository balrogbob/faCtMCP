#pragma once

#include "factmcp.h"

#include <string>

struct ExampleToolRegistration {
    const char* name;
    const char* tool_json;
    factmcp_tool_handler handler;
    void* user_data;
};

int run_example_stdio_server(const ExampleToolRegistration* registrations, int count);

std::string example_json_escape(const std::string& value);
