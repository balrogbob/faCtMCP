#include "RefactorHandler.h"
#include "../JsonUtils.h"
#include "../../editor_core/FileIO.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <set>

namespace fs = std::filesystem;

namespace {

std::string error_response(const std::string& cmd) {
    return "{\"success\":false,\"error\":\"Unknown command '" + json_escape(cmd) + "' for refactor tool. Use: extract, variable, inline, signature, move_file, split, merge\"}";
}

} // namespace

std::string handle_refactor(const std::string& params) {
    const std::string command = json_string_field(params, "command");
    if (command.empty()) {
        return R"({"success":false,"error":"Missing 'command' parameter for refactor tool. Use: extract, variable, inline, signature, move_file, split, merge"})";
    }

    std::error_code ec;

    if (command == "extract") {
        const std::string file_path = json_string_field(params, "path");
        const std::string func_name = json_string_field(params, "name");
        std::string start_str = json_number_field(params, "start_line");
        std::string end_str = json_number_field(params, "end_line");
        if (file_path.empty() || start_str.empty() || end_str.empty()) {
            return R"({"success":false,"error":"Missing required parameters: path, start_line, end_line"})";
        }
        int start_line = 0, end_line = 0;
        try { start_line = std::stoi(start_str); end_line = std::stoi(end_str); } catch (...) {
            return R"({"success":false,"error":"Invalid line numbers"})";
        }
        if (start_line < 1 || end_line < start_line) {
            return R"({"success":false,"error":"Invalid line range"})";
        }

        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }

        std::vector<std::string> lines;
        {
            std::istringstream stream(content.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }

        if (end_line > (int)lines.size()) end_line = (int)lines.size();

        // Extract the selected lines
        std::string extracted_body;
        for (int i = start_line - 1; i < end_line; ++i) {
            extracted_body += lines[i] + "\n";
        }

        // Detect variables used in the extracted block that are defined before it
        std::vector<std::string> params_used;
        std::regex var_re(R"(\b([a-zA-Z_]\w*)\b)");
        std::set<std::string> defined_before;
        for (int i = 0; i < start_line - 1; ++i) {
            std::smatch m;
            std::string l = lines[i];
            if (std::regex_search(l, m, var_re)) {
                defined_before.insert(m[1].str());
            }
        }
        // Find which of those are used in the extracted body
        for (const auto& v : defined_before) {
            if (extracted_body.find(v) != std::string::npos) {
                params_used.push_back(v);
            }
        }

        std::string fname = func_name.empty() ? "extracted_function" : func_name;
        std::string param_list;
        for (size_t i = 0; i < params_used.size(); ++i) {
            if (i > 0) param_list += ", ";
            param_list += "auto " + params_used[i];
        }

        // Build preview
        std::string preview = "void " + fname + "(" + param_list + ") {\n" + extracted_body + "}\n\n";
        std::string call_line = fname + "(";
        for (size_t i = 0; i < params_used.size(); ++i) {
            if (i > 0) call_line += ", ";
            call_line += params_used[i];
        }
        call_line += ");";

        return "{\"success\":true,\"preview\":{\"extracted_function\":\"" + json_escape(preview) +
               "\",\"call_site\":\"" + json_escape(call_line) +
               "\",\"start_line\":" + std::to_string(start_line) +
               ",\"end_line\":" + std::to_string(end_line) +
               ",\"parameters\":" + std::to_string(params_used.size()) + "}}";
    }

    if (command == "variable") {
        const std::string file_path = json_string_field(params, "path");
        const std::string var_name = json_string_field(params, "name");
        std::string line_str = json_number_field(params, "line");
        if (file_path.empty() || line_str.empty()) {
            return R"({"success":false,"error":"Missing required parameters: path, line"})";
        }
        int line_num = 0;
        try { line_num = std::stoi(line_str); } catch (...) {
            return R"({"success":false,"error":"Invalid line number"})";
        }

        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }

        std::vector<std::string> lines;
        {
            std::istringstream stream(content.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }

        if (line_num < 1 || line_num > (int)lines.size()) {
            return R"({"success":false,"error":"Line number out of range"})";
        }

        std::string target_line = lines[line_num - 1];
        std::string name = var_name.empty() ? "extracted_var" : var_name;

        // Find a complex expression on the line (assignment RHS, function call result, etc.)
        std::regex assign_re(R"((.+?)\s*=\s*(.+);)");
        std::smatch m;
        if (std::regex_search(target_line, m, assign_re)) {
            std::string lhs = m[1].str();
            std::string rhs = m[2].str();
            // Trim
            size_t ls = lhs.find_first_not_of(" \t");
            if (ls != std::string::npos) lhs = lhs.substr(ls);

            std::string preview = "auto " + name + " = " + rhs + ";\n" +
                                  lhs + " = " + name + ";";
            return "{\"success\":true,\"preview\":{\"declaration\":\"" + json_escape("auto " + name + " = " + rhs + ";") +
                   "\",\"replacement\":\"" + json_escape(lhs + " = " + name + ";") +
                   "\",\"line\":" + std::to_string(line_num) + "}}";
        }

        // Function call result
        std::regex call_re(R"(([\w:]+)\(([^)]*)\))");
        if (std::regex_search(target_line, m, call_re)) {
            std::string full_call = m[0].str();
            std::string preview = "auto " + name + " = " + full_call + ";";
            return "{\"success\":true,\"preview\":{\"declaration\":\"" + json_escape(preview) +
                   "\",\"line\":" + std::to_string(line_num) +
                   ",\"note\":\"Extract the function call result into a named variable\"}}";
        }

        return "{\"success\":true,\"preview\":{\"note\":\"No extractable expression found on line " +
               std::to_string(line_num) + "\"}}";
    }

    if (command == "inline") {
        const std::string file_path = json_string_field(params, "path");
        const std::string symbol_name = json_string_field(params, "symbol");
        if (file_path.empty() || symbol_name.empty()) {
            return R"({"success":false,"error":"Missing required parameters: path, symbol"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }

        // Find the function definition
        std::vector<std::string> lines;
        {
            std::istringstream stream(content.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }

        int def_start = -1, def_end = -1;
        std::string func_body;
        std::string func_params;
        std::regex func_re(R"((?:[\w:*&<>\s]+)\s+(\w+)\s*\(([^)]*)\))");

        for (int i = 0; i < (int)lines.size(); ++i) {
            std::smatch m;
            if (std::regex_search(lines[i], m, func_re) && m[1].str() == symbol_name) {
                def_start = i;
                func_params = m[2].str();
                // Find the body (brace counting)
                int braces = 0;
                for (int j = i; j < (int)lines.size(); ++j) {
                    for (char c : lines[j]) {
                        if (c == '{') ++braces;
                        if (c == '}') --braces;
                    }
                    if (braces <= 0 && j > i) {
                        def_end = j;
                        break;
                    }
                }
                // Extract body (skip first { line and last } line)
                for (int j = def_start + 1; j < def_end; ++j) {
                    func_body += lines[j] + "\n";
                }
                break;
            }
        }

        if (def_start < 0) {
            return "{\"success\":true,\"preview\":{\"note\":\"Function '" + json_escape(symbol_name) + "' not found in file\"}}";
        }

        // Count call sites
        int call_count = 0;
        std::string call_sites = "[";
        bool first = true;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (i >= def_start && i <= def_end) continue;
            if (lines[i].find(symbol_name + "(") != std::string::npos) {
                ++call_count;
                if (!first) call_sites += ",";
                call_sites += std::to_string(i + 1);
                first = false;
            }
        }
        call_sites += "]";

        return "{\"success\":true,\"preview\":{\"function\":\"" + json_escape(symbol_name) +
               "\",\"definition_lines\":\"" + std::to_string(def_start + 1) + "-" + std::to_string(def_end + 1) +
               "\",\"body\":\"" + json_escape(func_body) +
               "\",\"call_sites\":" + call_sites +
               ",\"call_count\":" + std::to_string(call_count) +
               ",\"note\":\"Inlining would replace " + std::to_string(call_count) + " call sites with the function body\"}}";
    }

    if (command == "signature") {
        const std::string file_path = json_string_field(params, "path");
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string new_params = json_string_field(params, "new_params");
        if (file_path.empty() || symbol_name.empty()) {
            return R"({"success":false,"error":"Missing required parameters: path, symbol"})";
        }

        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }

        std::vector<std::string> lines;
        {
            std::istringstream stream(content.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }

        // Find all locations with this function signature
        std::string changes = "[";
        bool first = true;
        std::regex func_re(R"(([\w:*&<>\s]+?)\s+(\w+)\s*\(([^)]*)\)(.*))");
        for (int i = 0; i < (int)lines.size(); ++i) {
            std::smatch m;
            if (std::regex_search(lines[i], m, func_re) && m[2].str() == symbol_name) {
                if (!first) changes += ",";
                changes += "{\"line\":" + std::to_string(i + 1) +
                           ",\"current\":\"" + json_escape(lines[i]) +
                           "\",\"current_params\":\"" + json_escape(m[3].str()) + "\"}";
                first = false;
            }
        }
        changes += "]";

        return "{\"success\":true,\"function\":\"" + json_escape(symbol_name) +
               "\",\"locations\":" + changes +
               ",\"note\":\"Pass 'new_params' to preview the signature change\"}";
    }

    if (command == "move_file") {
        const std::string file_path = json_string_field(params, "path");
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string dest_file = json_string_field(params, "destination");
        if (file_path.empty() || symbol_name.empty() || dest_file.empty()) {
            return R"({"success":false,"error":"Missing required parameters: path, symbol, destination"})";
        }

        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }

        // Find the symbol's definition
        std::vector<std::string> lines;
        {
            std::istringstream stream(content.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }

        int def_start = -1, def_end = -1;
        std::regex func_re(R"((?:[\w:*&<>\s]+)\s+(\w+)\s*\([^)]*\))");
        for (int i = 0; i < (int)lines.size(); ++i) {
            std::smatch m;
            if (std::regex_search(lines[i], m, func_re) && m[1].str() == symbol_name) {
                def_start = i;
                int braces = 0;
                for (int j = i; j < (int)lines.size(); ++j) {
                    for (char c : lines[j]) {
                        if (c == '{') ++braces;
                        if (c == '}') --braces;
                    }
                    if (braces <= 0 && j > i) { def_end = j; break; }
                }
                break;
            }
        }

        if (def_start < 0) {
            // Try class/struct
            std::regex class_re(R"((?:class|struct)\s+(\w+))");
            for (int i = 0; i < (int)lines.size(); ++i) {
                std::smatch m;
                if (std::regex_search(lines[i], m, class_re) && m[1].str() == symbol_name) {
                    def_start = i;
                    int braces = 0;
                    for (int j = i; j < (int)lines.size(); ++j) {
                        for (char c : lines[j]) {
                            if (c == '{') ++braces;
                            if (c == '}') --braces;
                        }
                        if (braces <= 0 && j > i) { def_end = j; break; }
                    }
                    break;
                }
            }
        }

        if (def_start < 0) {
            return "{\"success\":true,\"preview\":{\"note\":\"Symbol '" + json_escape(symbol_name) + "' not found\"}}";
        }

        // Extract the code to move
        std::string moved_code;
        for (int i = def_start; i <= def_end; ++i) {
            moved_code += lines[i] + "\n";
        }

        return "{\"success\":true,\"preview\":{\"symbol\":\"" + json_escape(symbol_name) +
               "\",\"source\":\"" + json_escape(file_path) +
               "\",\"destination\":\"" + json_escape(dest_file) +
               "\",\"lines\":\"" + std::to_string(def_start + 1) + "-" + std::to_string(def_end + 1) +
               "\",\"code\":\"" + json_escape(moved_code) +
               "\",\"note\":\"Move this code to " + json_escape(dest_file) + " and add #include if needed\"}}";
    }

    if (command == "split") {
        const std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }

        // Analyze the file for logical split points (classes, major functions)
        std::vector<std::string> lines;
        {
            std::istringstream stream(content.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }

        struct SplitGroup {
            std::string name;
            int start_line;
            int end_line;
        };
        std::vector<SplitGroup> groups;
        std::regex class_re(R"(^\s*(?:class|struct)\s+(\w+))");

        for (int i = 0; i < (int)lines.size(); ++i) {
            std::smatch m;
            if (std::regex_search(lines[i], m, class_re)) {
                int braces = 0;
                int class_end = i;
                for (int j = i; j < (int)lines.size(); ++j) {
                    for (char c : lines[j]) {
                        if (c == '{') ++braces;
                        if (c == '}') --braces;
                    }
                    if (braces <= 0 && j > i) { class_end = j; break; }
                }
                groups.push_back({m[1].str(), i + 1, class_end + 1});
            }
        }

        std::string json = "[";
        for (size_t i = 0; i < groups.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"name\":\"" + json_escape(groups[i].name) +
                    "\",\"start_line\":" + std::to_string(groups[i].start_line) +
                    ",\"end_line\":" + std::to_string(groups[i].end_line) + "}";
        }
        json += "]";

        std::string base = fs::path(file_path).stem().string();
        return "{\"success\":true,\"suggested_splits\":" + json +
               ",\"note\":\"Each class/struct could be moved to a separate file (e.g., " +
               json_escape(base) + "_ClassName.h)\"}";
    }

    if (command == "merge") {
        std::string file_a = json_string_field(params, "file_a");
        std::string file_b = json_string_field(params, "file_b");
        if (file_a.empty() || file_b.empty()) {
            return R"({"success":false,"error":"Missing 'file_a' or 'file_b' parameter"})";
        }
        auto content_a = FileIO::read(file_a);
        auto content_b = FileIO::read(file_b);
        if (!content_a || !content_b) {
            return R"({"success":false,"error":"Could not read one or both files"})";
        }

        // Count declarations in each file
        auto count_decls = [](const std::string& content) -> int {
            int count = 0;
            std::regex class_re(R"(^\s*(?:class|struct|enum)\s+\w+)");
            std::regex func_re(R"(^\s*(?:[\w:*&<>\s]+)\s+\w+\s*\([^)]*\))");
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) {
                std::smatch m;
                if (std::regex_search(line, m, class_re) || std::regex_search(line, m, func_re)) ++count;
            }
            return count;
        };

        int decls_a = count_decls(content_a.value());
        int decls_b = count_decls(content_b.value());
        int total_lines = (int)std::count(content_a.value().begin(), content_a.value().end(), '\n') +
                          (int)std::count(content_b.value().begin(), content_b.value().end(), '\n');

        // Check for overlapping includes
        std::regex inc_re(R"(^\s*#include\s+[<"]([^>"]+)[>"])");
        std::set<std::string> includes_a, includes_b;
        {
            std::istringstream sa(content_a.value());
            std::string line;
            while (std::getline(sa, line)) {
                std::smatch m;
                if (std::regex_search(line, m, inc_re)) includes_a.insert(m[1].str());
            }
        }
        {
            std::istringstream sb(content_b.value());
            std::string line;
            while (std::getline(sb, line)) {
                std::smatch m;
                if (std::regex_search(line, m, inc_re)) includes_b.insert(m[1].str());
            }
        }

        int shared_includes = 0;
        for (const auto& inc : includes_a) {
            if (includes_b.count(inc)) ++shared_includes;
        }

        return "{\"success\":true,\"preview\":{\"file_a\":\"" + json_escape(file_a) +
               "\",\"file_b\":\"" + json_escape(file_b) +
               "\",\"declarations_a\":" + std::to_string(decls_a) +
               ",\"declarations_b\":" + std::to_string(decls_b) +
               ",\"total_lines\":" + std::to_string(total_lines) +
               ",\"shared_includes\":" + std::to_string(shared_includes) +
               ",\"total_includes\":" + std::to_string(includes_a.size() + includes_b.size() - shared_includes) +
               ",\"note\":\"Merged file would combine " + std::to_string(decls_a + decls_b) +
               " declarations with deduplicated includes\"}}";
    }

    return error_response(command);
}
