#pragma once
#include <string>

// Build super-tool for compile/test/lint workflows.
// Params: { "command": "diagnostics|build|log|test|run|lint|format", ... }
std::string handle_build(const std::string& params);
