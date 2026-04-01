#pragma once
#include <string>

// Git super-tool for version control queries.
// Params: { "command": "diff|status|log|blame|branch|commit_files", ... }
std::string handle_git(const std::string& params);
