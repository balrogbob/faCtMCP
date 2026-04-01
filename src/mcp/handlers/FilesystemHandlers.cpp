#include "FilesystemHandlers.h"

#include "../JsonUtils.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <ctime>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string normalize_seps(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::string home_dir() {
    if (const char* user_profile = std::getenv("USERPROFILE")) {
        return user_profile;
    }
    const char* home_drive = std::getenv("HOMEDRIVE");
    const char* home_path = std::getenv("HOMEPATH");
    if (home_drive && home_path) {
        return std::string(home_drive) + home_path;
    }
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
    return ".";
}

std::string expand_home(const std::string& value) {
    if (value == "~") return home_dir();
    if (value.size() > 2 && value[0] == '~' && (value[1] == '/' || value[1] == '\\')) {
        return home_dir() + value.substr(1);
    }
    return value;
}

std::string resolve_path(const std::string& requested_path) {
    const std::string expanded = expand_home(requested_path);
    fs::path absolute = fs::path(expanded);
    if (!absolute.is_absolute()) {
        absolute = fs::absolute(absolute);
    } else {
        absolute = fs::weakly_canonical(absolute);
    }

    std::error_code ec;
    fs::path real = fs::weakly_canonical(absolute, ec);
    if (!ec) {
        return normalize_seps(real.string());
    }

    const fs::path parent = absolute.parent_path();
    if (parent.empty()) {
        throw std::runtime_error("Parent directory does not exist: " + normalize_seps(parent.string()));
    }

    (void)fs::weakly_canonical(parent, ec);
    if (ec) {
        throw std::runtime_error("Parent directory does not exist: " + normalize_seps(parent.string()));
    }

    return normalize_seps(absolute.string());
}

std::string file_time_to_string(const fs::file_time_type& ftime) {
    auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(system_time);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
    return buffer;
}

std::string format_size(uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024 * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
}

std::string json_array_of_strings(const std::vector<std::string>& items) {
    std::string json = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) json += ",";
        json += '"' + json_escape(items[i]) + '"';
    }
    json += "]";
    return json;
}

std::vector<std::string> json_string_array_field(const std::string& body, const std::string& key) {
    std::vector<std::string> out;
    const std::string needle = '"' + key + '"';
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return out;
    pos = body.find('[', pos);
    if (pos == std::string::npos) return out;

    bool in_string = false;
    bool escaping = false;
    size_t item_start = std::string::npos;
    for (size_t i = pos + 1; i < body.size(); ++i) {
        char c = body[i];
        if (in_string) {
            if (escaping) {
                escaping = false;
                continue;
            }
            if (c == '\\') {
                escaping = true;
                continue;
            }
            if (c == '"') {
                if (item_start != std::string::npos) {
                    out.push_back(json_unescape_string(body.substr(item_start, i - item_start)));
                    item_start = std::string::npos;
                }
                in_string = false;
            }
            continue;
        }

        if (c == ']') break;
        if (c == '"') {
            in_string = true;
            item_start = i + 1;
        }
    }
    return out;
}

std::string escape_json_bool(bool value) {
    return value ? "true" : "false";
}

bool wildcard_match(const std::string& text, const std::string& pattern) {
    const std::string p = normalize_seps(pattern);
    const std::string t = normalize_seps(text);

    size_t ti = 0, pi = 0;
    size_t star = std::string::npos, match = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
            ++ti;
            ++pi;
            continue;
        }
        if (pi < p.size() && p[pi] == '*') {
            star = ++pi;
            match = ti;
            continue;
        }
        if (star != std::string::npos) {
            pi = star;
            ti = ++match;
            continue;
        }
        return false;
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

bool matches_exclude(const std::string& relative_path, const std::vector<std::string>& patterns) {
    const std::string normalized = normalize_seps(relative_path);
    for (const auto& pattern : patterns) {
        if (pattern.empty()) continue;
        std::string glob = pattern;
        if (glob.find('*') == std::string::npos && glob.find('?') == std::string::npos) {
            glob = "**/" + glob + "/**";
        }
        if (wildcard_match(normalized, glob)) return true;
    }
    return false;
}

std::string entry_type(const fs::directory_entry& entry) {
    std::error_code ec;
    return entry.is_directory(ec) ? "directory" : "file";
}

std::string build_tree_json(const fs::path& dir, const fs::path& root, int depth, int max_depth, int& count) {
    std::error_code ec;
    std::vector<fs::directory_entry> entries;
    for (auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename().string() < b.path().filename().string();
    });

    std::string json = "[";
    bool first = true;
    for (const auto& entry : entries) {
        if (count >= 1000) break;
        std::error_code status_ec;
        if (entry.is_symlink(status_ec)) continue;

        if (!first) json += ",";
        first = false;
        ++count;

        const fs::path rel = fs::relative(entry.path(), root, ec);
        json += "{\"name\":\"" + json_escape(entry.path().filename().string()) + "\",";
        json += "\"path\":\"" + json_escape(normalize_seps(rel.string())) + "\",";
        json += "\"type\":\"" + entry_type(entry) + "\"";

        if (entry.is_directory(status_ec) && depth < max_depth) {
            json += ",\"children\":" + build_tree_json(entry.path(), root, depth + 1, max_depth, count);
        }

        json += "}";
    }
    json += "]";
    return json;
}

std::string file_info_json(const fs::path& p) {
    std::error_code ec;
    auto stats = fs::status(p, ec);
    if (ec) {
        return std::string("{\"success\":false,\"error\":\"") + json_escape(ec.message()) + "\"}";
    }

    fs::file_status symlink_stats = fs::symlink_status(p, ec);
    const bool is_symlink = !ec && fs::is_symlink(symlink_stats);

    uintmax_t size = 0;
    if (fs::is_regular_file(stats)) {
        size = fs::file_size(p, ec);
        if (ec) size = 0;
    }

    std::string created = "";
    std::string modified = "";
    std::string accessed = "";
    try { created = file_time_to_string(fs::last_write_time(p)); } catch (...) {}
    try { modified = file_time_to_string(fs::last_write_time(p)); } catch (...) {}
    try { accessed = modified; } catch (...) {}

    const std::string type = fs::is_directory(stats) ? "directory" : "file";
    const std::string permissions = is_symlink ? "l--" : "rwx";

    return std::string("{\"success\":true,\"size\":") + std::to_string(size) +
           ",\"created\":\"" + json_escape(created) +
           "\",\"modified\":\"" + json_escape(modified) +
           "\",\"accessed\":\"" + json_escape(accessed) +
           "\",\"isDirectory\":" + escape_json_bool(fs::is_directory(stats)) +
           ",\"isFile\":" + escape_json_bool(fs::is_regular_file(stats)) +
           ",\"permissions\":\"" + json_escape(permissions) +
           "\",\"type\":\"" + type + "\"}";
}

} // namespace

std::string handle_create_directory(const std::string& params) {
    const std::string path = json_string_field(params, "path");
    if (path.empty()) {
        return R"({"success":false,"error":"Missing 'path' parameter"})";
    }

    try {
        const std::string resolved = resolve_path(path);
        std::error_code ec;
        fs::create_directories(fs::path(resolved), ec);
        if (ec) {
            return std::string("{\"success\":false,\"error\":\"") + json_escape(ec.message()) + "\"}";
        }
        return std::string("{\"success\":true,\"path\":\"") + json_escape(resolved) + "\"}";
    } catch (const std::exception& e) {
        return std::string("{\"success\":false,\"error\":\"") + json_escape(e.what()) + "\"}";
    }
}

std::string handle_list_directory(const std::string& params) {
    const std::string path = json_string_field(params, "path");
    if (path.empty()) {
        return R"({"success":false,"error":"Missing 'path' parameter"})";
    }

    try {
        const std::string resolved = resolve_path(path);
        std::error_code ec;
        if (!fs::exists(resolved, ec) || !fs::is_directory(resolved, ec)) {
            return R"({"success":false,"error":"Path does not exist or is not a directory"})";
        }

        std::vector<fs::directory_entry> entries;
        for (auto& entry : fs::directory_iterator(resolved, fs::directory_options::skip_permission_denied, ec)) {
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename().string() < b.path().filename().string();
        });

        std::string json_entries = "[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) json_entries += ",";
            const auto& entry = entries[i];
            json_entries += "{\"name\":\"" + json_escape(entry.path().filename().string()) +
                            "\",\"type\":\"" + entry_type(entry) + "\"}";
        }
        json_entries += "]";

        return std::string("{\"success\":true,\"entries\":") + json_entries +
               ",\"count\":" + std::to_string(entries.size()) + "}";
    } catch (const std::exception& e) {
        return std::string("{\"success\":false,\"error\":\"") + json_escape(e.what()) + "\"}";
    }
}

std::string handle_list_directory_with_sizes(const std::string& params) {
    const std::string path = json_string_field(params, "path");
    const std::string sort_by = json_string_field(params, "sortBy");
    const std::string resolved_sort = sort_by.empty() ? "name" : sort_by;
    if (path.empty()) {
        return R"({"success":false,"error":"Missing 'path' parameter"})";
    }

    try {
        const std::string resolved = resolve_path(path);
        std::error_code ec;
        if (!fs::exists(resolved, ec) || !fs::is_directory(resolved, ec)) {
            return R"({"success":false,"error":"Path does not exist or is not a directory"})";
        }

        struct EntryInfo {
            std::string name;
            std::string type;
            uintmax_t size;
            std::string modified;
        };

        std::vector<EntryInfo> entries;
        for (auto& entry : fs::directory_iterator(resolved, fs::directory_options::skip_permission_denied, ec)) {
            EntryInfo info;
            info.name = entry.path().filename().string();
            info.type = entry_type(entry);
            info.size = 0;
            if (entry.is_regular_file(ec)) {
                info.size = entry.file_size(ec);
            }
            try {
                info.modified = file_time_to_string(fs::last_write_time(entry.path()));
            } catch (...) {
                info.modified = "";
            }
            entries.push_back(info);
        }

        std::sort(entries.begin(), entries.end(), [&](const EntryInfo& a, const EntryInfo& b) {
            if (resolved_sort == "size") {
                return a.size > b.size;
            }
            return a.name < b.name;
        });

        std::string json_entries = "[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) json_entries += ",";
            json_entries += "{\"name\":\"" + json_escape(entries[i].name) +
                            "\",\"type\":\"" + json_escape(entries[i].type) +
                            "\",\"size\":" + std::to_string(entries[i].size) +
                            ",\"sizeText\":\"" + json_escape(format_size(entries[i].size)) +
                            "\",\"modified\":\"" + json_escape(entries[i].modified) + "\"}";
        }
        json_entries += "]";

        uintmax_t total_size = 0;
        size_t file_count = 0;
        size_t dir_count = 0;
        for (const auto& entry : entries) {
            if (entry.type == "file") {
                ++file_count;
                total_size += entry.size;
            } else {
                ++dir_count;
            }
        }

        return std::string("{\"success\":true,\"entries\":") + json_entries +
               ",\"count\":" + std::to_string(entries.size()) +
               ",\"files\":" + std::to_string(file_count) +
               ",\"directories\":" + std::to_string(dir_count) +
               ",\"totalSize\":" + std::to_string(total_size) +
               ",\"totalSizeText\":\"" + json_escape(format_size(total_size)) + "\"}";
    } catch (const std::exception& e) {
        return std::string("{\"success\":false,\"error\":\"") + json_escape(e.what()) + "\"}";
    }
}

std::string handle_directory_tree(const std::string& params) {
    const std::string path = json_string_field(params, "path");
    if (path.empty()) {
        return R"({"success":false,"error":"Missing 'path' parameter"})";
    }

    try {
        const std::string resolved = resolve_path(path);
        std::error_code ec;
        if (!fs::exists(resolved, ec) || !fs::is_directory(resolved, ec)) {
            return R"({"success":false,"error":"Path does not exist or is not a directory"})";
        }

        int count = 0;
        std::string tree = build_tree_json(fs::path(resolved), fs::path(resolved), 0, 16, count);
        return std::string("{\"success\":true,\"tree\":") + tree +
               ",\"count\":" + std::to_string(count) + "}";
    } catch (const std::exception& e) {
        return std::string("{\"success\":false,\"error\":\"") + json_escape(e.what()) + "\"}";
    }
}

std::string handle_move_file_fs(const std::string& params) {
    const std::string source = json_string_field(params, "source");
    const std::string destination = json_string_field(params, "destination");
    if (source.empty() || destination.empty()) {
        return R"({"success":false,"error":"Missing 'source' or 'destination' parameter"})";
    }

    try {
        const std::string resolved_source = resolve_path(source);
        const std::string resolved_destination = resolve_path(destination);
        std::error_code ec;
        fs::rename(fs::path(resolved_source), fs::path(resolved_destination), ec);
        if (ec) {
            return std::string("{\"success\":false,\"error\":\"") + json_escape(ec.message()) + "\"}";
        }
        return std::string("{\"success\":true,\"source\":\"") + json_escape(resolved_source) +
               "\",\"destination\":\"" + json_escape(resolved_destination) + "\"}";
    } catch (const std::exception& e) {
        return std::string("{\"success\":false,\"error\":\"") + json_escape(e.what()) + "\"}";
    }
}

std::string handle_search_files(const std::string& params) {
    const std::string path = json_string_field(params, "path");
    const std::string pattern = json_string_field(params, "pattern");
    const std::vector<std::string> exclude_patterns = json_string_array_field(params, "excludePatterns");
    if (path.empty() || pattern.empty()) {
        return R"({"success":false,"error":"Missing 'path' or 'pattern' parameter"})";
    }

    try {
        const std::string resolved = resolve_path(path);
        std::error_code ec;
        if (!fs::exists(resolved, ec) || !fs::is_directory(resolved, ec)) {
            return R"({"success":false,"error":"Path does not exist or is not a directory"})";
        }

        std::vector<std::string> results;
        for (auto& entry : fs::recursive_directory_iterator(resolved, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_directory(ec) && !entry.is_regular_file(ec)) continue;

            const std::string rel = normalize_seps(fs::relative(entry.path(), resolved, ec).string());
            if (matches_exclude(rel, exclude_patterns)) continue;

            const std::string name = entry.path().filename().string();
            auto lower_name = name;
            auto lower_pattern = pattern;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lower_name.find(lower_pattern) != std::string::npos) {
                results.push_back(normalize_seps(entry.path().string()));
            }
        }

        std::sort(results.begin(), results.end());
        return std::string("{\"success\":true,\"results\":") + json_array_of_strings(results) +
               ",\"count\":" + std::to_string(results.size()) + "}";
    } catch (const std::exception& e) {
        return std::string("{\"success\":false,\"error\":\"") + json_escape(e.what()) + "\"}";
    }
}

std::string handle_get_file_info(const std::string& params) {
    const std::string path = json_string_field(params, "path");
    if (path.empty()) {
        return R"({"success":false,"error":"Missing 'path' parameter"})";
    }

    try {
        const std::string resolved = resolve_path(path);
        return file_info_json(fs::path(resolved));
    } catch (const std::exception& e) {
        return std::string("{\"success\":false,\"error\":\"") + json_escape(e.what()) + "\"}";
    }
}
