#pragma once
#include <string>

// Refactor super-tool for code transformation previews.
// Params: { "command": "extract|variable|inline|signature|move_file|split|merge", ... }
std::string handle_refactor(const std::string& params);
