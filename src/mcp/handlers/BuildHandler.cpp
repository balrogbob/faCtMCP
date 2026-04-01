#include "BuildHandler.h"
#include "../JsonUtils.h"
#include "../../editor_core/FileIO.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#define FACTMCP_POPEN _popen
#define FACTMCP_PCLOSE _pclose
#else
#define FACTMCP_POPEN popen
#define FACTMCP_PCLOSE pclose
#endif

namespace fs = std::filesystem;

namespace {

std::string exec_command(const std::string& cmd) {
    std::string result;
    FILE* pipe = FACTMCP_POPEN(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    FACTMCP_PCLOSE(pipe);
    return result;
}

struct Diagnostic {
    std::string severity;
    std::string file;
    int line;
    std::string message;
};

std::vector<Diagnostic> parse_msvc_output(const std::string& output) {
    std::vector<Diagnostic> diags;
    // MSVC format: file(line,column): error C####: message
    //              file(line,column): warning C####: message
    std::regex diag_re(R"((.+?)\((\d+)(?:,\d+)?\):\s+(error|warning)\s+(\w+):\s*(.*))");
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::smatch m;
        if (std::regex_search(line, m, diag_re)) {
            Diagnostic d;
            d.file = m[1].str();
            d.line = std::stoi(m[2].str());
            d.severity = m[3].str();
            d.message = m[4].str() + ": " + m[5].str();
            diags.push_back(d);
        }
    }
    return diags;
}

std::string diags_to_json(const std::vector<Diagnostic>& diags) {
    std::string json = "[";
    for (size_t i = 0; i < diags.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"severity\":\"" + json_escape(diags[i].severity) +
                "\",\"file\":\"" + json_escape(diags[i].file) +
                "\",\"line\":" + std::to_string(diags[i].line) +
                ",\"message\":\"" + json_escape(diags[i].message) + "\"}";
    }
    json += "]";
    return json;
}

std::string stub(const std::string& cmd) {
    return "{\"success\":true,\"command\":\"" + json_escape(cmd) + "\",\"result\":\"Not yet implemented\"}";
}

// Store last build output in a static so it can be queried.
static std::string g_last_build_output;

} // namespace

std::string handle_build(const std::string& params) {
    const std::string command = json_string_field(params, "command");
    if (command.empty()) {
        return R"({"success":false,"error":"Missing 'command' parameter for build tool"})";
    }

    std::string build_dir = json_string_field(params, "build_dir");

    if (command == "diagnostics") {
        if (g_last_build_output.empty()) {
            return R"({"success":true,"diagnostics":[],"count":0,"note":"No build output captured yet. Run 'build' first."})";
        }
        auto diags = parse_msvc_output(g_last_build_output);
        return "{\"success\":true,\"diagnostics\":" + diags_to_json(diags) + ",\"count\":" + std::to_string(diags.size()) + "}";
    }

    if (command == "build") {
        std::string dir = build_dir.empty() ? "." : build_dir;
        std::string target = json_string_field(params, "target");
        std::string cmd = "cd \"" + dir + "\" && cmake --build . --config Release";
        if (!target.empty()) cmd += " --target \"" + target + "\"";
        g_last_build_output = exec_command(cmd + " 2>&1");
        bool success = g_last_build_output.find("error") == std::string::npos &&
                       g_last_build_output.find("Build FAILED") == std::string::npos;
        auto diags = parse_msvc_output(g_last_build_output);
        return "{\"success\":" + std::string(success ? "true" : "false") +
               ",\"diagnostics\":" + diags_to_json(diags) +
               ",\"count\":" + std::to_string(diags.size()) + "}";
    }

    if (command == "log") {
        if (g_last_build_output.empty()) {
            return R"({"success":true,"log":"","note":"No build output captured yet"})";
        }
        // Truncate very long logs
        std::string log = g_last_build_output;
        if (log.size() > 50000) {
            log = log.substr(0, 50000) + "\n... [truncated]";
        }
        return "{\"success\":true,\"log\":\"" + json_escape(log) + "\"}";
    }

    if (command == "run") {
        const std::string cmd = json_string_field(params, "command_line");
        if (cmd.empty()) {
            return R"({"success":false,"error":"Missing 'command_line' parameter"})";
        }
        int timeout = 30;
        std::string timeout_str = json_number_field(params, "timeout");
        if (!timeout_str.empty()) {
            try { timeout = std::stoi(timeout_str); } catch (...) {}
        }
        std::string output = exec_command(cmd + " 2>&1");
        // Truncate long output
        if (output.size() > 50000) {
            output = output.substr(0, 50000) + "\n... [truncated]";
        }
        return "{\"success\":true,\"output\":\"" + json_escape(output) + "\"}";
    }

    if (command == "test") {
        std::string dir = build_dir.empty() ? "." : build_dir;
        std::string cmd = "cd \"" + dir + "\" && ctest --output-on-failure -C Release 2>&1";
        std::string output = exec_command(cmd);
        bool success = output.find("FAIL") == std::string::npos &&
                       output.find("Errors while running CTest") == std::string::npos;
        if (output.size() > 50000) output = output.substr(0, 50000) + "\n... [truncated]";
        return "{\"success\":" + std::string(success ? "true" : "false") +
               ",\"output\":\"" + json_escape(output) + "\"}";
    }

    if (command == "lint") {
        const std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter for lint"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        std::vector<std::string> issues;
        std::istringstream stream(content.value());
        std::string line;
        int line_num = 0;
        int trailing_ws = 0, long_lines = 0, tab_mix = 0;
        while (std::getline(stream, line)) {
            ++line_num;
            if (line.size() > 120) {
                issues.push_back("{\"line\":" + std::to_string(line_num) +
                                 ",\"issue\":\"Line exceeds 120 characters (" + std::to_string(line.size()) + ")\"}");
                ++long_lines;
            }
            if (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
                issues.push_back("{\"line\":" + std::to_string(line_num) + ",\"issue\":\"Trailing whitespace\"}");
                ++trailing_ws;
            }
            bool has_tab = line.find('\t') != std::string::npos;
            bool has_space_indent = line.size() > 1 && line[0] == ' ' && line[1] == ' ';
            if (has_tab && has_space_indent) {
                ++tab_mix;
            }
        }
        std::string json = "[";
        int limit = 0;
        for (size_t i = 0; i < issues.size() && limit < 50; ++i, ++limit) {
            if (i > 0) json += ",";
            json += issues[i];
        }
        json += "]";
        int total = trailing_ws + long_lines + tab_mix;
        return "{\"success\":true,\"issues\":" + json +
               ",\"summary\":{\"trailing_whitespace\":" + std::to_string(trailing_ws) +
               ",\"long_lines\":" + std::to_string(long_lines) +
               ",\"mixed_indentation\":" + std::to_string(tab_mix) +
               "},\"count\":" + std::to_string(total) + "}";
    }

    if (command == "format") {
        const std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter for format"})";
        }
        // Try clang-format first, fall back to basic whitespace cleanup
        std::string check_cmd = "clang-format --version 2>nul";
        std::string version = exec_command(check_cmd);
        if (!version.empty()) {
            std::string fmt_cmd = "clang-format -i \"" + file_path + "\" 2>&1";
            std::string output = exec_command(fmt_cmd);
            return "{\"success\":true,\"method\":\"clang-format\",\"output\":\"" + json_escape(output) + "\"}";
        }
        // Basic cleanup: strip trailing whitespace
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        std::istringstream stream(content.value());
        std::string cleaned;
        std::string line;
        bool changed = false;
        while (std::getline(stream, line)) {
            size_t end = line.find_last_not_of(" \t");
            std::string trimmed = (end != std::string::npos) ? line.substr(0, end + 1) : "";
            if (trimmed != line) changed = true;
            cleaned += trimmed + "\n";
        }
        if (changed) {
            FileIO::write(file_path, cleaned);
        }
        return "{\"success\":true,\"method\":\"basic_cleanup\",\"changed\":" +
               std::string(changed ? "true" : "false") + "}";
    }

    return "{\"success\":false,\"error\":\"Unknown command '" + json_escape(command) + "' for build tool. Use: diagnostics, build, log, test, run, lint, format\"}";
}
