#include "GrepHandler.h"

#include "../JsonUtils.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace fs = std::filesystem;

namespace {

struct FileListCacheEntry {
    std::vector<fs::path> files;
    std::chrono::steady_clock::time_point cached_at;
};

std::unordered_map<std::string, FileListCacheEntry>& file_list_cache() {
    static std::unordered_map<std::string, FileListCacheEntry> cache;
    return cache;
}

std::mutex& file_list_cache_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::string> split_glob_list(const std::string& value) {
    std::vector<std::string> patterns;
    std::string current;
    for (char c : value) {
        if (c == ',' || c == ';' || c == '\n' || c == '\r') {
            if (!current.empty()) {
                patterns.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        patterns.push_back(current);
    }
    return patterns;
}

// Check if a filename matches a simple glob pattern (case-insensitive on Windows).
bool filename_matches_glob(const std::string& name, const std::string& pattern) {
    std::string rx_str;
    rx_str.reserve(pattern.size() * 2);
    for (char c : pattern) {
        switch (c) {
            case '*': rx_str += ".*"; break;
            case '?': rx_str += "."; break;
            case '.': rx_str += "\\."; break;
            default:
                if (std::string("()[]{}+^$|").find(c) != std::string::npos) {
                    rx_str += '\\';
                }
                rx_str += c;
                break;
        }
    }
    try {
        std::regex rx(rx_str, std::regex::optimize | std::regex::icase);
        return std::regex_match(name, rx);
    } catch (...) {
        return false;
    }
}

bool path_matches_any_glob(const std::string& path, const std::string& pattern_list) {
    for (const auto& pattern : split_glob_list(pattern_list)) {
        if (pattern.empty()) continue;
        if (filename_matches_glob(path, pattern)) {
            return true;
        }
    }
    return false;
}

// Check if a file is likely binary by scanning the first 8KB for null bytes.
bool is_likely_binary(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return true;

    char buf[8192];
    file.read(buf, sizeof(buf));
    std::streamsize n = file.gcount();

    for (std::streamsize i = 0; i < n; ++i) {
        if (buf[i] == '\0') return true;
    }
    return false;
}

} // namespace

void invalidate_grep_cache() {
    std::lock_guard<std::mutex> lock(file_list_cache_mutex());
    file_list_cache().clear();
}

std::string handle_grep_files(const std::string& params) {
    const std::string pattern = json_string_field(params, "pattern");
    if (pattern.empty()) {
        return R"({"success": false, "matches": [], "error": "Missing 'pattern' parameter"})";
    }

    std::string root = json_string_field(params, "path");
    if (root.empty()) {
        root = ".";
    }

    std::string include_filter = json_string_field(params, "include");
    std::string case_sensitive_str = json_string_field(params, "case_sensitive");
    bool case_sensitive = (case_sensitive_str == "1" || case_sensitive_str == "true");
    std::string exclude_filter = json_string_field(params, "exclude");

    std::string max_str = json_number_field(params, "max_results");
    int max_results = 100;
    if (!max_str.empty()) {
        try { max_results = std::stoi(max_str); } catch (...) {}
    }
    if (max_results <= 0) max_results = 100;
    if (max_results > 10000) max_results = 10000;

    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return R"({"success": false, "matches": [], "error": "Path does not exist or is not a directory"})";
    }

    // Compile the search regex
    std::regex search_rx;
    try {
        auto flags = std::regex::optimize;
        if (!case_sensitive) {
            flags |= std::regex::icase;
        }
        search_rx = std::regex(pattern, flags);
    } catch (const std::regex_error& e) {
        return std::string("{\"success\": false, \"matches\": [], \"error\": \"Invalid regex: ") +
               json_escape(e.what()) + "\"}";
    }

    struct Match {
        std::string file;
        int line_number;
        std::string line;
    };

    std::vector<Match> matches;
    std::mutex matches_mutex;
    std::atomic<int> matched_count{0};
    std::atomic<bool> stop{false};

    std::vector<fs::path> candidate_files;
    {
        std::lock_guard<std::mutex> lock(file_list_cache_mutex());
        auto& cache = file_list_cache();
        auto it = cache.find(root);
        if (it != cache.end()) {
            candidate_files = it->second.files;
        }
    }

    if (candidate_files.empty()) {
        try {
            for (auto& entry : fs::recursive_directory_iterator(
                    root, fs::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                candidate_files.push_back(entry.path());
            }
        } catch (const std::exception& e) {
            return std::string("{\"success\": false, \"matches\": [], \"error\": \"") +
                   json_escape(e.what()) + "\"}";
        }

        std::lock_guard<std::mutex> lock(file_list_cache_mutex());
        file_list_cache()[root] = {candidate_files, std::chrono::steady_clock::now()};
    }

    std::vector<fs::path> filtered_files;
    filtered_files.reserve(candidate_files.size());
    for (const auto& path : candidate_files) {
        std::string rel = fs::relative(path, root, ec).generic_string();
        std::string filename = path.filename().string();

        if (!include_filter.empty() && !filename_matches_glob(filename, include_filter) && !filename_matches_glob(rel, include_filter)) {
            continue;
        }
        if (!exclude_filter.empty() && (path_matches_any_glob(rel, exclude_filter) || path_matches_any_glob(filename, exclude_filter))) {
            continue;
        }
        filtered_files.push_back(path);
    }

    std::atomic<size_t> next_index{0};
    const size_t worker_count = std::max<size_t>(1, std::min<size_t>(std::thread::hardware_concurrency() == 0 ? 4 : std::thread::hardware_concurrency(), 8));

    auto worker = [&]() {
        std::vector<Match> local_matches;
        while (true) {
            if (stop.load(std::memory_order_relaxed)) break;
            size_t index = next_index.fetch_add(1);
            if (index >= filtered_files.size()) break;

            const fs::path& path = filtered_files[index];

            std::ifstream file(path);
            if (!file) continue;

            std::string rel = fs::relative(path, root, ec).string();

            std::string line;
            int line_num = 0;
            while (std::getline(file, line)) {
                ++line_num;
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (std::regex_search(line, search_rx)) {
                    std::string display = line;
                    if (display.size() > 500) {
                        display = display.substr(0, 500) + "...";
                    }

                    local_matches.push_back({rel, line_num, display});
                    if (matched_count.fetch_add(1, std::memory_order_relaxed) + 1 >= max_results) {
                        stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                }
            }
        }

        if (!local_matches.empty()) {
            std::lock_guard<std::mutex> lock(matches_mutex);
            for (auto& match : local_matches) {
                if ((int)matches.size() >= max_results) break;
                matches.push_back(std::move(match));
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& thread : workers) {
        thread.join();
    }

    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        if (a.file != b.file) return a.file < b.file;
        return a.line_number < b.line_number;
    });

    // Build JSON matches array
    std::string json_matches;
    for (size_t i = 0; i < matches.size(); ++i) {
        if (i > 0) json_matches += ",";
        json_matches += "{\"file\":\"" + json_escape(matches[i].file) +
                        "\",\"line_number\":" + std::to_string(matches[i].line_number) +
                        ",\"line\":\"" + json_escape(matches[i].line) + "\"}";
    }

    return "{\"success\": true, \"matches\": [" + json_matches +
           "], \"count\": " + std::to_string(matches.size()) + "}";
}
