#pragma once

#include <string>
#include <vector>

namespace ToolDefinitions {

const std::vector<std::string>& AllToolNames();
std::string DefinitionJson(const std::string& tool_name);

} // namespace ToolDefinitions
