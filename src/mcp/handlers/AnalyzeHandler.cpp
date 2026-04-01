#include "AnalyzeHandler.h"
#include "../JsonUtils.h"
#include "../../editor_core/FileIO.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <algorithm>
#include <map>
#include <set>
#include <functional>
#include <tuple>

namespace fs = std::filesystem;

namespace {

struct ComplexityResult {
    std::string name;
    int line;
    int complexity;
};

std::vector<ComplexityResult> compute_complexity(const std::string& content) {
    std::vector<ComplexityResult> results;
    std::istringstream stream(content);
    std::string line;
    int line_num = 0;
    std::string current_func;
    int current_func_line = 0;
    int current_complexity = 0;
    int brace_depth = 0;

    while (std::getline(stream, line)) {
        ++line_num;
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));

        // Detect function start (simplified heuristic)
        std::regex func_re(R"(^\s*(?:[\w:*&<>\s]+)\s+(\w+)\s*\([^)]*\))");
        std::smatch m;
        if (std::regex_search(trimmed, m, func_re) && trimmed.find('{') != std::string::npos) {
            // Save previous function
            if (!current_func.empty() && current_complexity > 0) {
                results.push_back({current_func, current_func_line, current_complexity});
            }
            current_func = m[1].str();
            current_func_line = line_num;
            current_complexity = 1; // base complexity
            brace_depth = 1;
            continue;
        }

        if (!current_func.empty()) {
            // Count branching statements
            if (trimmed.find("if ") != std::string::npos || trimmed.find("if(") != std::string::npos) ++current_complexity;
            if (trimmed.find("else if") != std::string::npos) ++current_complexity;
            if (trimmed.find("for ") != std::string::npos || trimmed.find("for(") != std::string::npos) ++current_complexity;
            if (trimmed.find("while ") != std::string::npos || trimmed.find("while(") != std::string::npos) ++current_complexity;
            if (trimmed.find("case ") != std::string::npos) ++current_complexity;
            if (trimmed.find("&&") != std::string::npos) ++current_complexity;
            if (trimmed.find("||") != std::string::npos) ++current_complexity;
            if (trimmed.find("catch") != std::string::npos) ++current_complexity;

            // Track braces
            for (char c : trimmed) {
                if (c == '{') ++brace_depth;
                if (c == '}') {
                    --brace_depth;
                    if (brace_depth <= 0 && !current_func.empty()) {
                        results.push_back({current_func, current_func_line, current_complexity});
                        current_func.clear();
                        current_complexity = 0;
                    }
                }
            }
        }
    }

    // Save last function if still open
    if (!current_func.empty() && current_complexity > 0) {
        results.push_back({current_func, current_func_line, current_complexity});
    }

    return results;
}

std::string stub(const std::string& cmd) {
    return "{\"success\":true,\"command\":\"" + json_escape(cmd) + "\",\"result\":\"Not yet implemented\"}";
}

// Scan all C/C++ source files under root and collect function definitions + references.
struct FileScan {
    std::string path;
    std::string content;
};

std::vector<FileScan> scan_sources(const std::string& root, std::error_code& ec) {
    std::vector<FileScan> files;
    for (auto& entry : fs::recursive_directory_iterator(
            root, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".c" && ext != ".hpp") continue;
        auto content = FileIO::read(entry.path().string());
        if (content) {
            files.push_back({fs::relative(entry.path(), root, ec).string(), content.value()});
        }
    }
    return files;
}

} // namespace

std::string handle_analyze(const std::string& params) {
    const std::string command = json_string_field(params, "command");
    if (command.empty()) {
        return R"({"success":false,"error":"Missing 'command' parameter for analyze tool"})";
    }

    std::error_code ec;

    if (command == "complexity") {
        const std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        auto results = compute_complexity(content.value());
        std::string json = "[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"name\":\"" + json_escape(results[i].name) +
                    "\",\"line\":" + std::to_string(results[i].line) +
                    ",\"complexity\":" + std::to_string(results[i].complexity) + "}";
        }
        json += "]";
        return "{\"success\":true,\"functions\":" + json + ",\"count\":" + std::to_string(results.size()) + "}";
    }

    if (command == "long") {
        const std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        int threshold = 50;
        std::string thresh_str = json_number_field(params, "threshold");
        if (!thresh_str.empty()) {
            try { threshold = std::stoi(thresh_str); } catch (...) {}
        }

        std::vector<std::tuple<std::string, int, int>> funcs;
        std::istringstream stream(content.value());
        std::string line;
        int line_num = 0;
        std::string current_func;
        int current_start = 0;
        int brace_depth = 0;
        bool in_func = false;

        std::regex func_re(R"(^\s*(?:[\w:*&<>\s]+)\s+(\w+)\s*\([^)]*\))");
        while (std::getline(stream, line)) {
            ++line_num;
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            std::smatch m;
            if (!in_func && std::regex_search(trimmed, m, func_re) && trimmed.find('{') != std::string::npos) {
                current_func = m[1].str();
                current_start = line_num;
                brace_depth = 0;
                in_func = true;
                for (char c : trimmed) {
                    if (c == '{') ++brace_depth;
                    if (c == '}') --brace_depth;
                }
                if (brace_depth <= 0) {
                    int len = line_num - current_start + 1;
                    if (len >= threshold) {
                        funcs.push_back({current_func, current_start, len});
                    }
                    in_func = false;
                }
            } else if (in_func) {
                for (char c : trimmed) {
                    if (c == '{') ++brace_depth;
                    if (c == '}') --brace_depth;
                }
                if (brace_depth <= 0) {
                    int len = line_num - current_start + 1;
                    if (len >= threshold) {
                        funcs.push_back({current_func, current_start, len});
                    }
                    in_func = false;
                }
            }
        }

        std::string json = "[";
        for (size_t i = 0; i < funcs.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"name\":\"" + json_escape(std::get<0>(funcs[i])) +
                    "\",\"line\":" + std::to_string(std::get<1>(funcs[i])) +
                    ",\"lines\":" + std::to_string(std::get<2>(funcs[i])) + "}";
        }
        json += "]";
        return "{\"success\":true,\"functions\":" + json + ",\"count\":" + std::to_string(funcs.size()) +
               ",\"threshold\":" + std::to_string(threshold) + "}";
    }

    if (command == "dead_code") {
        std::string root = json_string_field(params, "path");
        if (root.empty()) root = ".";

        // Collect all function definitions
        struct DefInfo { std::string file; int line; };
        std::map<std::string, std::vector<DefInfo>> definitions;
        auto files = scan_sources(root, ec);
        for (const auto& f : files) {
            std::istringstream stream(f.content);
            std::string line;
            int line_num = 0;
            std::regex func_re(R"(^\s*(?:[\w:*&<>\s]+)\s+(\w+)\s*\([^)]*\))");
            while (std::getline(stream, line)) {
                ++line_num;
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                if (trimmed.empty() || trimmed[0] == '#' || trimmed.substr(0, 2) == "//") continue;
                std::smatch m;
                if (std::regex_search(trimmed, m, func_re) && trimmed.find('{') != std::string::npos) {
                    std::string name = m[1].str();
                    if (name != "if" && name != "for" && name != "while" && name != "switch" &&
                        name != "return" && name != "catch" && name != "sizeof") {
                        definitions[name].push_back({f.path, line_num});
                    }
                }
            }
        }

        // For each definition, check if the name appears in other files
        std::string json = "[";
        bool first = true;
        int count = 0;
        for (const auto& [name, defs] : definitions) {
            if (count >= 200) break;
            for (const auto& d : defs) {
                int ref_count = 0;
                for (const auto& f : files) {
                    if (f.path == d.file) continue;
                    if (f.content.find(name) != std::string::npos) ++ref_count;
                }
                if (ref_count == 0) {
                    if (!first) json += ",";
                    json += "{\"name\":\"" + json_escape(name) + "\",\"file\":\"" +
                            json_escape(d.file) + "\",\"line\":" + std::to_string(d.line) + "}";
                    first = false;
                    ++count;
                }
            }
        }
        json += "]";
        return "{\"success\":true,\"dead_code\":" + json + ",\"count\":" + std::to_string(count) +
               ",\"note\":\"Heuristic: function definitions with no references in other files\"}";
    }

    if (command == "unused") {
        std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }

        // Collect #include directives
        struct IncInfo { std::string header; int line; };
        std::vector<IncInfo> includes;
        std::istringstream stream(content.value());
        std::string line;
        int line_num = 0;
        std::regex inc_re(R"(^\s*#include\s+[<"]([^>"]+)[>"])");
        while (std::getline(stream, line)) {
            ++line_num;
            std::smatch m;
            if (std::regex_search(line, m, inc_re)) {
                includes.push_back({m[1].str(), line_num});
            }
        }

        // Check if symbols from each include are used in the file body
        std::string json = "[";
        bool first = true;
        for (const auto& inc : includes) {
            // Simple heuristic: extract the basename without extension
            std::string base = inc.header;
            size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos) base = base.substr(slash + 1);
            size_t dot = base.find('.');
            if (dot != std::string::npos) base = base.substr(0, dot);

            // Skip common utility headers that are hard to trace
            if (base == "stdafx" || base == "pch" || base.empty()) continue;

            // Check if the base name (or something like it) appears in the content
            if (content.value().find(base) == std::string::npos || 
                content.value().find("#include") == content.value().find(base)) {
                // The only occurrence might be the #include itself
                size_t pos = 0;
                int usage_count = 0;
                while ((pos = content.value().find(base, pos)) != std::string::npos) {
                    ++usage_count;
                    pos += base.size();
                }
                // If only one occurrence (the include itself), mark as potentially unused
                if (usage_count <= 1) {
                    if (!first) json += ",";
                    json += "{\"include\":\"" + json_escape(inc.header) + "\",\"line\":" +
                            std::to_string(inc.line) + ",\"reason\":\"No usage of '" +
                            json_escape(base) + "' found outside the include directive\"}";
                    first = false;
                }
            }
        }
        json += "]";
        return "{\"success\":true,\"unused_includes\":" + json +
               ",\"note\":\"Heuristic: checks if include basename appears in file body\"}";
    }

    if (command == "cycles") {
        std::string root = json_string_field(params, "path");
        if (root.empty()) root = ".";

        // Build include graph
        std::map<std::string, std::vector<std::string>> graph;
        auto files = scan_sources(root, ec);
        std::regex inc_re(R"(^\s*#include\s+[<"]([^>"]+)[>"])");
        for (const auto& f : files) {
            std::istringstream stream(f.content);
            std::string line;
            while (std::getline(stream, line)) {
                std::smatch m;
                if (std::regex_search(line, m, inc_re)) {
                    graph[f.path].push_back(m[1].str());
                }
            }
        }

        // Detect cycles using DFS
        std::vector<std::vector<std::string>> cycles;
        std::set<std::string> visited;
        std::vector<std::string> path;

        std::function<void(const std::string&)> dfs = [&](const std::string& node) {
            if (visited.count(node)) return;
            visited.insert(node);
            path.push_back(node);
            if (graph.count(node)) {
                for (const auto& dep : graph[node]) {
                    // Check if dep is in our path (cycle)
                    auto it = std::find(path.begin(), path.end(), dep);
                    if (it != path.end()) {
                        std::vector<std::string> cycle(it, path.end());
                        cycle.push_back(dep);
                        cycles.push_back(cycle);
                    } else {
                        dfs(dep);
                    }
                }
            }
            path.pop_back();
        };

        for (const auto& f : files) {
            dfs(f.path);
            visited.clear();
            path.clear();
        }

        std::string json = "[";
        for (size_t i = 0; i < cycles.size() && i < 50; ++i) {
            if (i > 0) json += ",";
            json += "[";
            for (size_t j = 0; j < cycles[i].size(); ++j) {
                if (j > 0) json += ",";
                json += "\"" + json_escape(cycles[i][j]) + "\"";
            }
            json += "]";
        }
        json += "]";
        return "{\"success\":true,\"cycles\":" + json + ",\"count\":" + std::to_string(cycles.size()) + "}";
    }

    if (command == "duplicate") {
        std::string root = json_string_field(params, "path");
        if (root.empty()) root = ".";
        int min_block = 5;
        std::string mb_str = json_number_field(params, "min_lines");
        if (!mb_str.empty()) {
            try { min_block = std::stoi(mb_str); } catch (...) {}
        }
        if (min_block < 3) min_block = 3;

        auto files = scan_sources(root, ec);

        // Compare every pair of files for matching line blocks
        struct DupBlock { std::string file_a; std::string file_b; int start_a; int start_b; int lines; };
        std::vector<DupBlock> dups;

        for (size_t a = 0; a < files.size() && dups.size() < 50; ++a) {
            std::vector<std::string> a_lines;
            {
                std::istringstream stream(files[a].content);
                std::string line;
                while (std::getline(stream, line)) {
                    std::string t = line;
                    t.erase(0, t.find_first_not_of(" \t"));
                    if (!t.empty() && t.back() == '\r') t.pop_back();
                    a_lines.push_back(t);
                }
            }
            for (size_t b = a + 1; b < files.size() && dups.size() < 50; ++b) {
                std::vector<std::string> b_lines;
                {
                    std::istringstream stream(files[b].content);
                    std::string line;
                    while (std::getline(stream, line)) {
                        std::string t = line;
                        t.erase(0, t.find_first_not_of(" \t"));
                        if (!t.empty() && t.back() == '\r') t.pop_back();
                        b_lines.push_back(t);
                    }
                }
                for (size_t i = 0; i + min_block <= a_lines.size(); ++i) {
                    for (size_t j = 0; j + min_block <= b_lines.size(); ++j) {
                        int match = 0;
                        while (i + match < a_lines.size() && j + match < b_lines.size() &&
                               a_lines[i + match] == b_lines[j + match] && !a_lines[i + match].empty()) {
                            ++match;
                        }
                        if (match >= min_block) {
                            dups.push_back({files[a].path, files[b].path, (int)i + 1, (int)j + 1, match});
                            j += match - 1; // skip ahead
                        }
                    }
                }
            }
        }

        std::string json = "[";
        for (size_t i = 0; i < dups.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"file_a\":\"" + json_escape(dups[i].file_a) +
                    "\",\"file_b\":\"" + json_escape(dups[i].file_b) +
                    "\",\"start_a\":" + std::to_string(dups[i].start_a) +
                    ",\"start_b\":" + std::to_string(dups[i].start_b) +
                    ",\"lines\":" + std::to_string(dups[i].lines) + "}";
        }
        json += "]";
        return "{\"success\":true,\"duplicates\":" + json + ",\"count\":" + std::to_string(dups.size()) + "}";
    }

    if (command == "naming") {
        std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        std::vector<std::string> issues;
        std::istringstream stream(content.value());
        std::string line;
        int line_num = 0;

        std::regex class_re(R"(^\s*(?:class|struct|enum\s+class|enum)\s+(\w+))");
        std::regex func_re(R"(^\s*(?:[\w:*&<>\s]+?)\s+(\w+)\s*\([^)]*\))");
        std::regex var_re(R"(^\s*(?:const\s+)?(?:[\w:*&<>]+)\s+(\w+)\s*(?:=|;|,))");

        while (std::getline(stream, line)) {
            ++line_num;
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            if (trimmed.empty() || trimmed[0] == '#' || trimmed.substr(0, 2) == "//") continue;
            std::smatch m;

            // Classes/structs should be PascalCase
            if (std::regex_search(trimmed, m, class_re)) {
                std::string name = m[1].str();
                if (!name.empty() && (name[0] < 'A' || name[0] > 'Z')) {
                    issues.push_back("{\"line\":" + std::to_string(line_num) +
                                     ",\"symbol\":\"" + json_escape(name) +
                                     "\",\"issue\":\"Class/struct name should be PascalCase\"}");
                }
            }
            // Functions should be snake_case or PascalCase (C++ convention)
            if (std::regex_search(trimmed, m, func_re)) {
                std::string name = m[1].str();
                if (name != "if" && name != "for" && name != "while" && name != "switch" &&
                    name != "return" && name != "catch") {
                    bool has_upper = false, has_lower = false, has_underscore = false;
                    for (char c : name) {
                        if (c >= 'A' && c <= 'Z') has_upper = true;
                        if (c >= 'a' && c <= 'z') has_lower = true;
                        if (c == '_') has_underscore = true;
                    }
                    // Mixed case without underscores or snake_case with leading caps
                    if (has_upper && has_lower && !has_underscore && name.find('_') == std::string::npos) {
                        // camelCase - might be intentional, flag as info
                    } else if (has_upper && has_underscore) {
                        issues.push_back("{\"line\":" + std::to_string(line_num) +
                                         ",\"symbol\":\"" + json_escape(name) +
                                         "\",\"issue\":\"Function name mixes uppercase and underscores\"}");
                    }
                }
            }
        }

        std::string json = "[";
        int limit = 0;
        for (size_t i = 0; i < issues.size() && limit < 50; ++i, ++limit) {
            if (i > 0) json += ",";
            json += issues[i];
        }
        json += "]";
        return "{\"success\":true,\"issues\":" + json + ",\"count\":" + std::to_string(issues.size()) + "}";
    }

    if (command == "ast") {
        std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        std::vector<std::string> decls;
        std::istringstream stream(content.value());
        std::string line;
        int line_num = 0;

        std::regex class_re(R"(^\s*(?:class|struct|enum\s+class|enum)\s+(\w+)(?:\s*:\s*(.+))?)");
        std::regex func_re(R"(^\s*(?:[\w:*&<>\s]+?)\s+(\w+)\s*\(([^)]*)\))");
        std::regex var_re(R"(^\s*(?:const\s+)?(?:static\s+)?(?:extern\s+)?(?:[\w:*&<>]+(?:\s+[\w:*&<>]+)*)\s+(\w+)\s*(?:=|;|,))");
        std::regex typedef_re(R"(^\s*typedef\s+[\w:*&<>\s]+\s+(\w+))");
        std::regex using_re(R"(^\s*using\s+(\w+)\s*=)");
        std::regex namespace_re(R"(^\s*namespace\s+(\w+))");
        std::regex inc_re(R"(^\s*#include\s+[<"]([^>"]+)[>"])");
        std::regex define_re(R"(^\s*#define\s+(\w+))");

        while (std::getline(stream, line)) {
            ++line_num;
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            if (trimmed.empty() || trimmed.substr(0, 2) == "//") continue;
            std::smatch m;

            if (std::regex_search(trimmed, m, inc_re)) {
                decls.push_back("{\"kind\":\"include\",\"name\":\"" + json_escape(m[1].str()) +
                                "\",\"line\":" + std::to_string(line_num) + "}");
            } else if (std::regex_search(trimmed, m, define_re)) {
                decls.push_back("{\"kind\":\"define\",\"name\":\"" + json_escape(m[1].str()) +
                                "\",\"line\":" + std::to_string(line_num) + "}");
            } else if (std::regex_search(trimmed, m, namespace_re)) {
                decls.push_back("{\"kind\":\"namespace\",\"name\":\"" + json_escape(m[1].str()) +
                                "\",\"line\":" + std::to_string(line_num) + "}");
            } else if (std::regex_search(trimmed, m, class_re)) {
                std::string bases = m[2].matched ? m[2].str() : "";
                decls.push_back("{\"kind\":\"class\",\"name\":\"" + json_escape(m[1].str()) +
                                "\",\"bases\":\"" + json_escape(bases) +
                                "\",\"line\":" + std::to_string(line_num) + "}");
            } else if (std::regex_search(trimmed, m, func_re)) {
                decls.push_back("{\"kind\":\"function\",\"name\":\"" + json_escape(m[1].str()) +
                                "\",\"params\":\"" + json_escape(m[2].str()) +
                                "\",\"line\":" + std::to_string(line_num) + "}");
            } else if (std::regex_search(trimmed, m, typedef_re)) {
                decls.push_back("{\"kind\":\"typedef\",\"name\":\"" + json_escape(m[1].str()) +
                                "\",\"line\":" + std::to_string(line_num) + "}");
            } else if (std::regex_search(trimmed, m, using_re)) {
                decls.push_back("{\"kind\":\"using\",\"name\":\"" + json_escape(m[1].str()) +
                                "\",\"line\":" + std::to_string(line_num) + "}");
            }
        }

        std::string json = "[";
        for (size_t i = 0; i < decls.size(); ++i) {
            if (i > 0) json += ",";
            json += decls[i];
        }
        json += "]";
        return "{\"success\":true,\"declarations\":" + json + ",\"count\":" + std::to_string(decls.size()) + "}";
    }

    if (command == "preprocessor") {
        std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        std::vector<std::string> directives;
        std::istringstream stream(content.value());
        std::string line;
        int line_num = 0;

        std::regex pp_re(R"(^\s*#\s*(\w+)(?:\s+(.*))?)");
        while (std::getline(stream, line)) {
            ++line_num;
            std::smatch m;
            if (std::regex_search(line, m, pp_re)) {
                std::string directive = m[1].str();
                std::string arg = m[2].matched ? m[2].str() : "";
                directives.push_back("{\"directive\":\"" + json_escape(directive) +
                                     "\",\"arg\":\"" + json_escape(arg) +
                                     "\",\"line\":" + std::to_string(line_num) + "}");
            }
        }

        std::string json = "[";
        for (size_t i = 0; i < directives.size(); ++i) {
            if (i > 0) json += ",";
            json += directives[i];
        }
        json += "]";
        return "{\"success\":true,\"directives\":" + json + ",\"count\":" + std::to_string(directives.size()) + "}";
    }

    if (command == "include_graph") {
        std::string root = json_string_field(params, "path");
        if (root.empty()) root = ".";

        // Build include graph for all header/source files
        auto files = scan_sources(root, ec);
        std::regex inc_re(R"(^\s*#include\s+[<"]([^>"]+)[>"])");

        std::string edges = "[";
        bool first = true;
        for (const auto& f : files) {
            std::istringstream stream(f.content);
            std::string line;
            while (std::getline(stream, line)) {
                std::smatch m;
                if (std::regex_search(line, m, inc_re)) {
                    if (!first) edges += ",";
                    edges += "{\"from\":\"" + json_escape(f.path) +
                             "\",\"to\":\"" + json_escape(m[1].str()) + "\"}";
                    first = false;
                }
            }
        }
        edges += "]";
        return "{\"success\":true,\"edges\":" + edges + ",\"files\":" + std::to_string(files.size()) + "}";
    }

    return "{\"success\":false,\"error\":\"Unknown command '" + json_escape(command) + "' for analyze tool. Use: complexity, dead_code, unused, cycles, long, duplicate, naming, ast, preprocessor, include_graph\"}";
}
