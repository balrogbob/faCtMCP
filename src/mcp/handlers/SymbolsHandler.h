#pragma once
#include <string>

// Symbols super-tool for code navigation.
// Params: { "command": "get|def|ref|signature|doc|type|inherit|hierarchy", ... }
std::string handle_symbols(const std::string& params);
