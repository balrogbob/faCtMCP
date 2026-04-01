#pragma once
#include <string>

// Project super-tool for file/workspace structure.
// Params: { "command": "list|info|tree|root|config|diff|recent|workspace", ... }
std::string handle_project(const std::string& params);
