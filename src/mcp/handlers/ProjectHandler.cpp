#include "ProjectHandler.h"
#include "../JsonUtils.h"
#include "../../editor_core/FileIO.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

namespace {

std::string format_time(fs::file_time_type ftime) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
    return buf;
}

std::string file_size_str(uintmax_t size) {
    if (size < 1024) return std::to_string(size) + " B";
    if (size < 1024 * 1024) return std::to_string(size / 1024) + " KB";
    return std::to_string(size / (1024 * 1024)) + " MB";
}

std::string stub(const std::string& cmd) {
    return "{\"success\":true,\"command\":\"" + json_escape(cmd) + "\",\"result\":\"Not yet implemented\"}";
}

} // namespace

std::string handle_project(const std::string& params) {
    const std::string command = json_string_field(params, "command");
    if (command.empty()) {
        return R"({"success":false,"error":"Missing 'command' parameter for project tool"})";
    }

    std::string path = json_string_field(params, "path");
    if (path.empty()) path = ".";

    std::error_code ec;

    if (command == "list") {
        if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
            return R"({"success":false,"error":"Path does not exist or is not a directory"})";
        }
        std::string entries = "[";
        bool first = true;
        int count = 0;
        for (auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
            if (count++ >= 500) break;
            if (!first) entries += ",";
            std::string name = entry.path().filename().string();
            std::string type = entry.is_directory(ec) ? "directory" : "file";
            std::string size = entry.is_regular_file(ec) ? file_size_str(entry.file_size(ec)) : "";
            entries += "{\"name\":\"" + json_escape(name) + "\",\"type\":\"" + type + "\",\"size\":\"" + json_escape(size) + "\"}";
            first = false;
        }
        entries += "]";
        return "{\"success\":true,\"entries\":" + entries + ",\"count\":" + std::to_string(count) + "}";
    }

    if (command == "info") {
        if (!fs::exists(path, ec)) {
            return R"({"success":false,"error":"File not found"})";
        }
        fs::path p(path);
        std::string type = fs::is_directory(p, ec) ? "directory" : "file";
        std::string size = fs::is_regular_file(p, ec) ? file_size_str(fs::file_size(p, ec)) : "";
        std::string modified = "";
        try {
            auto ftime = fs::last_write_time(p);
            modified = format_time(ftime);
        } catch (...) {}

        int line_count = 0;
        if (fs::is_regular_file(p, ec)) {
            auto content = FileIO::read(path);
            if (content) {
                for (char c : content.value()) {
                    if (c == '\n') ++line_count;
                }
            }
        }

        return "{\"success\":true,\"name\":\"" + json_escape(p.filename().string()) +
               "\",\"type\":\"" + type +
               "\",\"size\":\"" + json_escape(size) +
               "\",\"modified\":\"" + json_escape(modified) +
               "\",\"lines\":" + std::to_string(line_count) + "}";
    }

    if (command == "tree") {
        std::string tree_json = "[";
        bool first = true;
        int count = 0;
        int max_depth = 4;
        std::string max_depth_str = json_number_field(params, "max_depth");
        if (!max_depth_str.empty()) {
            try { max_depth = std::stoi(max_depth_str); } catch (...) {}
        }

        auto build_tree = [&](auto&& self, const fs::path& dir, int depth) -> void {
            if (depth > max_depth || count >= 1000) return;
            for (auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                if (count++ >= 1000) break;
                if (!first) tree_json += ",";
                std::string name = entry.path().filename().string();
                std::string rel = fs::relative(entry.path(), path, ec).string();
                std::string type = entry.is_directory(ec) ? "directory" : "file";
                tree_json += "{\"path\":\"" + json_escape(rel) + "\",\"name\":\"" + json_escape(name) + "\",\"type\":\"" + type + "\"}";
                first = false;
                if (entry.is_directory(ec) && depth < max_depth) {
                    self(self, entry.path(), depth + 1);
                }
            }
        };

        try {
            build_tree(build_tree, fs::path(path), 0);
        } catch (...) {}
        tree_json += "]";
        return "{\"success\":true,\"tree\":" + tree_json + ",\"count\":" + std::to_string(count) + "}";
    }

    if (command == "root") {
        fs::path p = fs::absolute(path, ec);
        while (true) {
            if (fs::exists(p / ".git", ec) || fs::exists(p / "CMakeLists.txt", ec) ||
                fs::exists(p / "package.json", ec) || fs::exists(p / "Cargo.toml", ec) ||
                fs::exists(p / "Makefile", ec) || fs::exists(p / ".project", ec)) {
                return "{\"success\":true,\"root\":\"" + json_escape(p.string()) + "\"}";
            }
            if (!p.has_parent_path() || p == p.parent_path()) break;
            p = p.parent_path();
        }
        return R"({"success":false,"error":"Could not detect project root"})";
    }

    if (command == "config") {
        fs::path p = fs::absolute(path, ec);
        while (true) {
            std::vector<std::string> found;
            for (auto& name : {"CMakeLists.txt", "package.json", "Cargo.toml", "Makefile", "meson.build", ".gitignore"}) {
                if (fs::exists(p / name, ec)) {
                    found.push_back(name);
                }
            }
            if (!found.empty()) {
                std::string json = "[";
                for (size_t i = 0; i < found.size(); ++i) {
                    if (i > 0) json += ",";
                    json += "\"" + json_escape(found[i]) + "\"";
                }
                json += "]";
                return "{\"success\":true,\"root\":\"" + json_escape(p.string()) + "\",\"files\":" + json + "}";
            }
            if (!p.has_parent_path() || p == p.parent_path()) break;
            p = p.parent_path();
        }
        return R"({"success":false,"error":"No config files found"})";
    }

    if (command == "diff") {
        const std::string file_a = json_string_field(params, "file_a");
        const std::string file_b = json_string_field(params, "file_b");
        if (file_a.empty() || file_b.empty()) {
            return R"({"success":false,"error":"Missing 'file_a' or 'file_b' parameter"})";
        }
        auto content_a = FileIO::read(file_a);
        auto content_b = FileIO::read(file_b);
        if (!content_a || !content_b) {
            return R"({"success":false,"error":"Could not read one or both files"})";
        }
        // Simple line-by-line diff
        std::istringstream a_stream(content_a.value());
        std::istringstream b_stream(content_b.value());
        std::string a_line, b_line;
        int a_num = 0, b_num = 0;
        std::string hunks = "[";
        bool first = true;
        while (std::getline(a_stream, a_line)) {
            ++a_num;
            if (!std::getline(b_stream, b_line)) {
                if (!first) hunks += ",";
                hunks += "{\"type\":\"removed\",\"line\":" + std::to_string(a_num) + ",\"text\":\"" + json_escape(a_line) + "\"}";
                first = false;
                continue;
            }
            ++b_num;
            if (a_line != b_line) {
                if (!first) hunks += ",";
                hunks += "{\"type\":\"changed\",\"a_line\":" + std::to_string(a_num) + ",\"b_line\":" + std::to_string(b_num) +
                         ",\"a_text\":\"" + json_escape(a_line) + "\",\"b_text\":\"" + json_escape(b_line) + "\"}";
                first = false;
            }
        }
        while (std::getline(b_stream, b_line)) {
            ++b_num;
            if (!first) hunks += ",";
            hunks += "{\"type\":\"added\",\"line\":" + std::to_string(b_num) + ",\"text\":\"" + json_escape(b_line) + "\"}";
            first = false;
        }
        hunks += "]";
        return "{\"success\":true,\"hunks\":" + hunks + "}";
    }

    if (command == "recent") {
        int max_count = 20;
        std::string max_str = json_number_field(params, "max");
        if (!max_str.empty()) {
            try { max_count = std::stoi(max_str); } catch (...) {}
        }
        if (max_count <= 0) max_count = 20;
        if (max_count > 200) max_count = 200;

        if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
            return R"({"success":false,"error":"Path does not exist or is not a directory"})";
        }

        struct FileInfo {
            std::string path;
            std::string modified;
            fs::file_time_type mtime;
        };
        std::vector<FileInfo> files;

        for (auto& entry : fs::recursive_directory_iterator(
                path, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            try {
                auto ftime = fs::last_write_time(entry.path());
                std::string rel = fs::relative(entry.path(), path, ec).string();
                files.push_back({rel, format_time(ftime), ftime});
            } catch (...) {}
        }

        // Sort by modification time, newest first
        std::sort(files.begin(), files.end(),
                  [](const FileInfo& a, const FileInfo& b) { return a.mtime > b.mtime; });

        if ((int)files.size() > max_count) files.resize(max_count);

        std::string json = "[";
        for (size_t i = 0; i < files.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"path\":\"" + json_escape(files[i].path) +
                    "\",\"modified\":\"" + json_escape(files[i].modified) + "\"}";
        }
        json += "]";
        return "{\"success\":true,\"files\":" + json + ",\"count\":" + std::to_string(files.size()) + "}";
    }

    if (command == "workspace") {
        fs::path p = fs::absolute(path, ec);
        std::string root;
        fs::path check = p;
        while (true) {
            if (fs::exists(check / ".git", ec) || fs::exists(check / "CMakeLists.txt", ec) ||
                fs::exists(check / "package.json", ec) || fs::exists(check / "Cargo.toml", ec)) {
                root = check.string();
                break;
            }
            if (!check.has_parent_path() || check == check.parent_path()) break;
            check = check.parent_path();
        }

        int file_count = 0;
        int dir_count = 0;
        if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
            for (auto& entry : fs::recursive_directory_iterator(
                    p, fs::directory_options::skip_permission_denied, ec)) {
                if (entry.is_directory(ec)) ++dir_count;
                else if (entry.is_regular_file(ec)) ++file_count;
                if (file_count + dir_count > 100000) break;
            }
        }

        return "{\"success\":true,\"root\":\"" + json_escape(root) +
               "\",\"path\":\"" + json_escape(p.string()) +
               "\",\"files\":" + std::to_string(file_count) +
               ",\"directories\":" + std::to_string(dir_count) + "}";
    }

    return "{\"success\":false,\"error\":\"Unknown command '" + json_escape(command) + "' for project tool. Use: list, info, tree, root, config, diff, recent, workspace\"}";
}
