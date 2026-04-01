#include "GitHandler.h"
#include "../JsonUtils.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <array>
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

// Execute a shell command and capture stdout.
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

// Find the git root from a given path.
std::string find_git_root(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::absolute(path, ec);
    while (true) {
        if (fs::exists(p / ".git", ec)) return p.string();
        if (!p.has_parent_path() || p == p.parent_path()) break;
        p = p.parent_path();
    }
    return "";
}

std::string stub(const std::string& cmd) {
    return "{\"success\":true,\"command\":\"" + json_escape(cmd) + "\",\"result\":\"Not yet implemented\"}";
}

} // namespace

std::string handle_git(const std::string& params) {
    const std::string command = json_string_field(params, "command");
    if (command.empty()) {
        return R"({"success":false,"error":"Missing 'command' parameter for git tool"})";
    }

    std::string path = json_string_field(params, "path");
    if (path.empty()) path = ".";

    std::string git_root = find_git_root(path);
    if (git_root.empty()) {
        return R"({"success":false,"error":"Not inside a git repository"})";
    }

    if (command == "diff") {
        std::string staged = json_string_field(params, "staged");
        std::string cmd = "cd \"" + git_root + "\" && git diff";
        if (staged == "true") cmd += " --cached";
        std::string file = json_string_field(params, "file");
        if (!file.empty()) cmd += " -- \"" + file + "\"";
        std::string output = exec_command(cmd);
        return "{\"success\":true,\"diff\":\"" + json_escape(output) + "\"}";
    }

    if (command == "status") {
        std::string output = exec_command("cd \"" + git_root + "\" && git status --porcelain=v1 -u");
        std::vector<std::string> entries;
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) entries.push_back(line);
        }
        std::string json = "[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) json += ",";
            std::string status_code = entries[i].substr(0, 2);
            std::string filepath = entries[i].size() > 3 ? entries[i].substr(3) : "";
            json += "{\"status\":\"" + json_escape(status_code) + "\",\"file\":\"" + json_escape(filepath) + "\"}";
        }
        json += "]";
        return "{\"success\":true,\"files\":" + json + ",\"count\":" + std::to_string(entries.size()) + "}";
    }

    if (command == "log") {
        std::string max_str = json_number_field(params, "max");
        int max_count = 20;
        if (!max_str.empty()) {
            try { max_count = std::stoi(max_str); } catch (...) {}
        }
        if (max_count <= 0) max_count = 20;
        if (max_count > 200) max_count = 200;
        std::string cmd = "cd \"" + git_root + "\" && git log --oneline --no-merges -" + std::to_string(max_count);
        std::string output = exec_command(cmd);
        std::string commits = "[";
        std::istringstream stream(output);
        std::string line;
        bool first = true;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            if (!first) commits += ",";
            std::string hash = line.substr(0, line.find(' '));
            std::string msg = line.size() > hash.size() + 1 ? line.substr(hash.size() + 1) : "";
            commits += "{\"hash\":\"" + json_escape(hash) + "\",\"message\":\"" + json_escape(msg) + "\"}";
            first = false;
        }
        commits += "]";
        return "{\"success\":true,\"commits\":" + commits + "}";
    }

    if (command == "blame") {
        const std::string file_path = json_string_field(params, "file");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'file' parameter for blame"})";
        }
        std::string cmd = "cd \"" + git_root + "\" && git blame --porcelain -- \"" + file_path + "\"";
        std::string output = exec_command(cmd);
        if (output.empty()) {
            return R"({"success":false,"error":"Blame failed or file not tracked"})";
        }
        return "{\"success\":true,\"blame\":\"" + json_escape(output) + "\"}";
    }

    if (command == "branch") {
        std::string current = exec_command("cd \"" + git_root + "\" && git branch --show-current");
        // Trim trailing whitespace
        while (!current.empty() && (current.back() == '\n' || current.back() == '\r' || current.back() == ' ')) {
            current.pop_back();
        }
        std::string remote = exec_command("cd \"" + git_root + "\" && git rev-parse --abbrev-ref --symbolic-full-name @{u} 2>nul");
        while (!remote.empty() && (remote.back() == '\n' || remote.back() == '\r' || remote.back() == ' ')) {
            remote.pop_back();
        }
        return "{\"success\":true,\"current\":\"" + json_escape(current) + "\",\"remote\":\"" + json_escape(remote) + "\"}";
    }

    if (command == "commit_files") {
        std::string hash = json_string_field(params, "hash");
        if (hash.empty()) hash = "HEAD";
        std::string cmd = "cd \"" + git_root + "\" && git diff-tree --no-commit-id --name-status -r " + hash;
        std::string output = exec_command(cmd);
        std::string files = "[";
        std::istringstream stream(output);
        std::string line;
        bool first = true;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            if (!first) files += ",";
            std::string status = line.substr(0, 1);
            std::string name = line.size() > 2 ? line.substr(2) : "";
            files += "{\"status\":\"" + json_escape(status) + "\",\"file\":\"" + json_escape(name) + "\"}";
            first = false;
        }
        files += "]";
        return "{\"success\":true,\"files\":" + files + "}";
    }

    return "{\"success\":false,\"error\":\"Unknown command '" + json_escape(command) + "' for git tool. Use: diff, status, log, blame, branch, commit_files\"}";
}
