#include "ToolState.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>

namespace {

std::string trim_ascii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(start, end - start);
}

std::mutex& tool_state_mutex() {
    static std::mutex mutex;
    return mutex;
}

const std::vector<std::string>& tool_names_storage() {
    static const std::vector<std::string> names = {
        "read_file",
        "write_file",
        "edit_file",
        "create_directory",
        "list_directory",
        "list_directory_with_sizes",
        "directory_tree",
        "move_file",
        "search_files",
        "get_file_info",
        "glob_files",
        "grep_files",
        "symbols",
        "git",
        "project",
        "build",
        "editor",
        "analyze",
        "refactor",
        "bash",
        "terminal",
        "sequentialthinking",
        "create_entities",
        "create_relations",
        "add_observations",
        "delete_entities",
        "delete_observations",
        "delete_relations",
        "read_graph",
        "search_nodes",
        "open_nodes"
    };
    return names;
}

std::map<std::string, bool>& tool_enabled_storage() {
    static std::map<std::string, bool> enabled = []() {
        std::map<std::string, bool> initial;
        for (const auto& name : tool_names_storage()) {
            initial[name] = true;
        }
        return initial;
    }();
    return enabled;
}

} // namespace

namespace ToolState {

const std::vector<std::string>& AllToolNames() {
    return tool_names_storage();
}

bool IsEnabled(const std::string& tool_name) {
    std::lock_guard<std::mutex> lock(tool_state_mutex());
    const auto& enabled = tool_enabled_storage();
    auto it = enabled.find(tool_name);
    if (it == enabled.end()) {
        return true;
    }
    return it->second;
}

void SetEnabled(const std::string& tool_name, bool enabled) {
    std::lock_guard<std::mutex> lock(tool_state_mutex());
    auto& states = tool_enabled_storage();
    auto it = states.find(tool_name);
    if (it != states.end()) {
        it->second = enabled;
    }
}

void SetAllEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(tool_state_mutex());
    auto& states = tool_enabled_storage();
    for (auto& entry : states) {
        entry.second = enabled;
    }
}

void LoadDisabledCsv(const std::string& disabled_csv) {
    std::lock_guard<std::mutex> lock(tool_state_mutex());
    auto& states = tool_enabled_storage();
    for (auto& entry : states) {
        entry.second = true;
    }

    std::stringstream stream(disabled_csv);
    std::string item;
    while (std::getline(stream, item, ',')) {
        const std::string trimmed = trim_ascii(item);
        if (trimmed.empty()) {
            continue;
        }
        auto it = states.find(trimmed);
        if (it != states.end()) {
            it->second = false;
        }
    }
}

std::string DisabledCsv() {
    std::lock_guard<std::mutex> lock(tool_state_mutex());
    const auto& states = tool_enabled_storage();
    std::string result;
    for (const auto& name : tool_names_storage()) {
        auto it = states.find(name);
        if (it != states.end() && !it->second) {
            if (!result.empty()) {
                result += ",";
            }
            result += name;
        }
    }
    return result;
}

} // namespace ToolState
