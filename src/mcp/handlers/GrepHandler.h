#pragma once
#include <string>

// MCP handler for grep-like content search across files.
// Params JSON: { "pattern": "regex", "path": "C:/src", "include": "*.cpp", "max_results": 100 }
// Returns JSON: { "success": true, "matches": [{ "file": "...", "line_number": N, "line": "..." }], "count": N }
std::string handle_grep_files(const std::string& params);
void invalidate_grep_cache();
