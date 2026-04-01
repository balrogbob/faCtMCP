#pragma once
#include <string>

// Analyze super-tool for code analysis.
// Params: { "command": "complexity|dead_code|unused|cycles|long|duplicate|naming|ast|preprocessor|include_graph", ... }
std::string handle_analyze(const std::string& params);
