#pragma once
#include <string>

// MCP handler for POSIX-like glob file matching.
// Params JSON: { "pattern": "**/*.cpp", "path": "C:/src" }
// Returns JSON: { "success": true, "files": [...], "count": N }
std::string handle_glob_files(const std::string& params);
