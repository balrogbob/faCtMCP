#pragma once

#include <string>
#include <vector>

namespace ToolState {

const std::vector<std::string>& AllToolNames();
bool IsEnabled(const std::string& tool_name);
void SetEnabled(const std::string& tool_name, bool enabled);
void SetAllEnabled(bool enabled);
void LoadDisabledCsv(const std::string& disabled_csv);
std::string DisabledCsv();

} // namespace ToolState
