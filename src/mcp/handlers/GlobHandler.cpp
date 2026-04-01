#include "GlobHandler.h"

#include "../JsonUtils.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

// Normalize backslashes to forward slashes for consistent matching.
std::string normalize_seps(const std::string& s) {
    std::string out = s;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

// Convert a glob pattern (with forward slashes only) to a regex string.
// Input must already be normalized via normalize_seps().
std::string glob_to_regex_str(const std::string& glob) {
    std::string rx;
    rx.reserve(glob.size() * 2);

    for (size_t i = 0; i < glob.size(); ++i) {
        char c = glob[i];
        switch (c) {
            case '*':
                if (i + 1 < glob.size() && glob[i + 1] == '*') {
                    // ** matches any number of directories
                    ++i; // consume second '*'
                    // Skip optional separator after **/
                    if (i + 1 < glob.size() && glob[i + 1] == '/') {
                        ++i;
                    }
                    rx += "(?:.*/)?";
                } else {
                    // * matches any chars except path separator
                    rx += "[^/]*";
                }
                break;
            case '?':
                rx += "[^/]";
                break;
            case '.':
                rx += "\\.";
                break;
            case '/':
                rx += "/";
                break;
            default:
                if (std::string("()[]{}+^$|\\").find(c) != std::string::npos) {
                    rx += '\\';
                }
                rx += c;
                break;
        }
    }

    return rx;
}

// Check if a filename matches a simple glob (no path separators).
bool filename_matches(const std::string& name, const std::string& pattern) {
    std::string rx_str;
    rx_str.reserve(pattern.size() * 2);
    for (char c : pattern) {
        switch (c) {
            case '*': rx_str += ".*"; break;
            case '?': rx_str += "."; break;
            case '.': rx_str += "\\."; break;
            default:
                if (std::string("()[]{}+^$|\\").find(c) != std::string::npos) {
                    rx_str += '\\';
                }
                rx_str += c;
                break;
        }
    }
    try {
        std::regex rx(rx_str, std::regex::icase);
        return std::regex_match(name, rx);
    } catch (...) {
        return false;
    }
}

} // namespace

std::string handle_glob_files(const std::string& params) {
    const std::string raw_pattern = json_string_field(params, "pattern");
    if (raw_pattern.empty()) {
        return R"({"success": false, "files": [], "error": "Missing 'pattern' parameter"})";
    }

    std::string root = json_string_field(params, "path");
    if (root.empty()) {
        root = ".";
    }

    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return R"({"success": false, "files": [], "error": "Path does not exist or is not a directory"})";
    }

    // Normalize pattern to forward slashes for consistent matching
    const std::string pattern = normalize_seps(raw_pattern);

    // Determine if pattern has path separators (full path match) or just filename.
    bool has_sep = pattern.find('/') != std::string::npos;

    // Check if pattern uses ** (recursive)
    bool recursive = pattern.find("**") != std::string::npos;

    // Pre-build the regex for path matching (case-insensitive for Windows)
    std::regex path_rx;
    if (has_sep) {
        path_rx = std::regex(glob_to_regex_str(pattern), std::regex::icase);
    }

    std::vector<std::string> files;
    const int max_results = 500;

    try {
        if (recursive) {
            for (auto& entry : fs::recursive_directory_iterator(
                    root, fs::directory_options::skip_permission_denied, ec)) {
                if ((int)files.size() >= max_results) break;
                if (!entry.is_regular_file(ec)) continue;

                std::string rel = normalize_seps(fs::relative(entry.path(), root, ec).string());

                if (has_sep) {
                    if (std::regex_match(rel, path_rx)) {
                        files.push_back(rel);
                    }
                } else {
                    if (filename_matches(entry.path().filename().string(), raw_pattern)) {
                        files.push_back(rel);
                    }
                }
            }
        } else {
            for (auto& entry : fs::directory_iterator(
                    root, fs::directory_options::skip_permission_denied, ec)) {
                if ((int)files.size() >= max_results) break;
                if (!entry.is_regular_file(ec)) continue;

                std::string rel = normalize_seps(fs::relative(entry.path(), root, ec).string());

                if (has_sep) {
                    if (std::regex_match(rel, path_rx)) {
                        files.push_back(rel);
                    }
                } else {
                    if (filename_matches(entry.path().filename().string(), raw_pattern)) {
                        files.push_back(rel);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        return std::string("{\"success\": false, \"files\": [], \"error\": \"") + json_escape(e.what()) + "\"}";
    }

    // Sort results
    std::sort(files.begin(), files.end());

    // Build JSON array
    std::string json_files;
    for (size_t i = 0; i < files.size(); ++i) {
        if (i > 0) json_files += ",";
        json_files += "\"" + json_escape(files[i]) + "\"";
    }

    return "{\"success\": true, \"files\": [" + json_files + "], \"count\": " + std::to_string(files.size()) + "}";
}
