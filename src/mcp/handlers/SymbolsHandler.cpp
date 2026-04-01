#include "SymbolsHandler.h"
#include "../JsonUtils.h"
#include "../../editor_core/FileIO.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <map>

namespace fs = std::filesystem;

namespace {

// Simple symbol extraction: scan for C/C++/C# style definitions.
struct Symbol {
    std::string name;
    std::string kind;
    int line;
};

std::vector<Symbol> extract_symbols(const std::string& content) {
    std::vector<Symbol> symbols;
    std::istringstream stream(content);
    std::string line;
    int line_num = 0;

    std::regex class_re(R"(^\s*(?:class|struct|enum\s+class|enum)\s+(\w+))");
    std::regex func_re(R"(^\s*(?:[\w:*&<>\s]+)\s+(\w+)\s*\([^)]*\)\s*(?:const|override|final|noexcept|->\s*[\w:*&<>]+)*\s*\{?)");
    std::regex typedef_re(R"(^\s*typedef\s+[\w:*&<>\s]+\s+(\w+))");
    std::regex using_re(R"(^\s*using\s+(\w+)\s*=)");

    while (std::getline(stream, line)) {
        ++line_num;

        // Skip preprocessor, comments, empty lines
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (trimmed.empty() || trimmed[0] == '#' || trimmed.substr(0, 2) == "//") continue;

        std::smatch m;
        if (std::regex_search(trimmed, m, class_re)) {
            std::string kw = trimmed.substr(0, trimmed.find(m[1].str()));
            std::string kind = "class";
            if (kw.find("struct") != std::string::npos) kind = "struct";
            else if (kw.find("enum") != std::string::npos) kind = "enum";
            symbols.push_back({m[1].str(), kind, line_num});
        } else if (std::regex_search(trimmed, m, func_re)) {
            // Filter out control-flow keywords
            std::string name = m[1].str();
            if (name != "if" && name != "for" && name != "while" && name != "switch" &&
                name != "return" && name != "catch" && name != "sizeof" && name != "static_cast" &&
                name != "dynamic_cast" && name != "reinterpret_cast" && name != "const_cast") {
                symbols.push_back({name, "function", line_num});
            }
        } else if (std::regex_search(trimmed, m, typedef_re)) {
            symbols.push_back({m[1].str(), "typedef", line_num});
        } else if (std::regex_search(trimmed, m, using_re)) {
            symbols.push_back({m[1].str(), "using", line_num});
        }
    }

    return symbols;
}

std::string symbols_to_json(const std::vector<Symbol>& symbols) {
    std::string json = "[";
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + json_escape(symbols[i].name) +
                "\",\"kind\":\"" + json_escape(symbols[i].kind) +
                "\",\"line\":" + std::to_string(symbols[i].line) + "}";
    }
    json += "]";
    return json;
}

std::string error_response(const std::string& cmd) {
    return "{\"success\":false,\"error\":\"Unknown command '" + json_escape(cmd) + "' for symbols tool. Use: get, def, ref, signature, doc, type, inherit, hierarchy\"}";
}

} // namespace

std::string handle_symbols(const std::string& params) {
    const std::string command = json_string_field(params, "command");
    if (command.empty()) {
        return R"({"success":false,"error":"Missing 'command' parameter for symbols tool"})";
    }

    std::error_code ec;

    if (command == "get") {
        const std::string file_path = json_string_field(params, "path");
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        auto syms = extract_symbols(content.value());
        return "{\"success\":true,\"symbols\":" + symbols_to_json(syms) + ",\"count\":" + std::to_string(syms.size()) + "}";
    }

    if (command == "def") {
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string file_path = json_string_field(params, "path");
        if (symbol_name.empty()) {
            return R"({"success":false,"error":"Missing 'symbol' parameter"})";
        }
        // Search in specified file or current directory
        std::string search_path = file_path.empty() ? "." : file_path;
        if (fs::is_regular_file(search_path, ec)) {
            auto content = FileIO::read(search_path);
            if (!content) return R"({"success":false,"error":"File not found"})";
            auto syms = extract_symbols(content.value());
            std::string results = "[";
            bool first = true;
            for (const auto& s : syms) {
                if (s.name == symbol_name) {
                    if (!first) results += ",";
                    results += "{\"name\":\"" + json_escape(s.name) + "\",\"kind\":\"" + json_escape(s.kind) + "\",\"line\":" + std::to_string(s.line) + ",\"file\":\"" + json_escape(search_path) + "\"}";
                    first = false;
                }
            }
            results += "]";
            return "{\"success\":true,\"definitions\":" + results + "}";
        }
        return R"({"success":false,"error":"Path must be a file for 'def' command"})";
    }

    if (command == "ref") {
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string search_path = json_string_field(params, "path");
        if (symbol_name.empty()) {
            return R"({"success":false,"error":"Missing 'symbol' parameter"})";
        }
        std::string root = search_path.empty() ? "." : search_path;
        if (!fs::exists(root, ec)) {
            return R"({"success":false,"error":"Path does not exist"})";
        }

        std::string results = "[";
        bool first = true;
        int count = 0;

        auto search_file = [&](const fs::path& p) {
            auto content = FileIO::read(p.string());
            if (!content) return;
            std::istringstream stream(content.value());
            std::string line;
            int line_num = 0;
            while (std::getline(stream, line) && count < 500) {
                ++line_num;
                if (line.find(symbol_name) != std::string::npos) {
                    if (!first) results += ",";
                    std::string rel = fs::relative(p, root, ec).string();
                    results += "{\"file\":\"" + json_escape(rel) + "\",\"line\":" + std::to_string(line_num) + ",\"text\":\"" + json_escape(line) + "\"}";
                    first = false;
                    ++count;
                }
            }
        };

        if (fs::is_regular_file(root, ec)) {
            search_file(root);
        } else {
            for (auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
                if (count >= 500) break;
                if (!entry.is_regular_file(ec)) continue;
                std::string ext = entry.path().extension().string();
                if (ext == ".cpp" || ext == ".h" || ext == ".c" || ext == ".hpp" || ext == ".tcl") {
                    search_file(entry.path());
                }
            }
        }
        results += "]";
        return "{\"success\":true,\"references\":" + results + ",\"count\":" + std::to_string(count) + "}";
    }

    if (command == "signature") {
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string file_path = json_string_field(params, "path");
        if (symbol_name.empty()) {
            return R"({"success":false,"error":"Missing 'symbol' parameter"})";
        }
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        std::istringstream stream(content.value());
        std::string line;
        int line_num = 0;
        std::string results = "[";
        bool first = true;

        while (std::getline(stream, line)) {
            ++line_num;
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            if (trimmed.empty() || trimmed[0] == '#' || trimmed.substr(0, 2) == "//") continue;

            if (trimmed.find(symbol_name) != std::string::npos) {
                // Check if this is a function declaration/definition containing the symbol
                std::regex sig_re(R"(([\w:*&<>\s]+)\s+(\w+)\s*\(([^)]*)\)\s*(.*))");
                std::smatch m;
                if (std::regex_search(trimmed, m, sig_re) && m[2].str() == symbol_name) {
                    std::string ret_type = m[1].str();
                    // Trim leading whitespace from return type
                    size_t start = ret_type.find_first_not_of(" \t");
                    if (start != std::string::npos) ret_type = ret_type.substr(start);
                    std::string params_str = m[3].str();
                    std::string suffix = m[4].str();
                    if (!first) results += ",";
                    results += "{\"line\":" + std::to_string(line_num) +
                               ",\"return_type\":\"" + json_escape(ret_type) +
                               "\",\"parameters\":\"" + json_escape(params_str) +
                               "\",\"suffix\":\"" + json_escape(suffix) +
                               "\",\"full\":\"" + json_escape(trimmed) + "\"}";
                    first = false;
                }
            }
        }
        results += "]";
        return "{\"success\":true,\"signatures\":" + results + "}";
    }

    if (command == "doc") {
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string file_path = json_string_field(params, "path");
        if (symbol_name.empty()) {
            return R"({"success":false,"error":"Missing 'symbol' parameter"})";
        }
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        std::istringstream stream(content.value());
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(stream, line)) {
            // Strip trailing CR
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }

        // Find the symbol declaration line
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string trimmed = lines[i];
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            if (trimmed.find(symbol_name) == std::string::npos) continue;
            // Check it looks like a declaration (not just a reference)
            if (trimmed.find('(') == std::string::npos && trimmed.find("class ") == std::string::npos &&
                trimmed.find("struct ") == std::string::npos && trimmed.find("enum ") == std::string::npos) {
                continue;
            }

            // Collect comment lines above this declaration
            std::string doc_comment;
            bool found_doc = false;
            // Walk upward collecting // or /* */ or /// style comments
            for (int j = (int)i - 1; j >= 0; --j) {
                std::string cline = lines[j];
                cline.erase(0, cline.find_first_not_of(" \t"));
                if (cline.empty()) {
                    if (found_doc) break; // blank line before doc block ends it
                    continue;
                }
                if (cline.substr(0, 3) == "///" || cline.substr(0, 2) == "//" ||
                    cline.substr(0, 2) == "/*" || cline.substr(0, 1) == "*" ||
                    cline.substr(0, 2) == " *") {
                    // Strip comment markers
                    std::string cleaned = cline;
                    if (cleaned.substr(0, 3) == "///") cleaned = cleaned.substr(3);
                    else if (cleaned.substr(0, 2) == "//") cleaned = cleaned.substr(2);
                    else if (cleaned.substr(0, 2) == "/*") cleaned = cleaned.substr(2);
                    else if (cleaned.substr(0, 2) == " *") cleaned = cleaned.substr(2);
                    else if (cleaned[0] == '*') cleaned = cleaned.substr(1);
                    // Trim leading space
                    size_t s = cleaned.find_first_not_of(" ");
                    if (s != std::string::npos) cleaned = cleaned.substr(s);
                    if (!doc_comment.empty()) doc_comment = cleaned + "\n" + doc_comment;
                    else doc_comment = cleaned;
                    found_doc = true;
                } else {
                    if (found_doc) break;
                }
            }
            if (!doc_comment.empty()) {
                return "{\"success\":true,\"symbol\":\"" + json_escape(symbol_name) +
                       "\",\"line\":" + std::to_string(i + 1) +
                       ",\"doc\":\"" + json_escape(doc_comment) + "\"}";
            }
            return "{\"success\":true,\"symbol\":\"" + json_escape(symbol_name) +
                   "\",\"line\":" + std::to_string(i + 1) +
                   ",\"doc\":\"\",\"note\":\"No doc comment found above this symbol\"}";
        }
        return "{\"success\":true,\"symbol\":\"" + json_escape(symbol_name) + "\",\"doc\":\"\",\"note\":\"Symbol not found in file\"}";
    }

    if (command == "type") {
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string file_path = json_string_field(params, "path");
        if (symbol_name.empty()) {
            return R"({"success":false,"error":"Missing 'symbol' parameter"})";
        }
        if (file_path.empty()) {
            return R"({"success":false,"error":"Missing 'path' parameter"})";
        }
        auto content = FileIO::read(file_path);
        if (!content) {
            return R"({"success":false,"error":"File not found or unreadable"})";
        }
        std::istringstream stream(content.value());
        std::string line;
        int line_num = 0;
        std::string results = "[";
        bool first = true;

        while (std::getline(stream, line)) {
            ++line_num;
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            if (trimmed.empty() || trimmed[0] == '#' || trimmed.substr(0, 2) == "//") continue;

            // Variable/field declarations: type name;
            std::regex var_re(R"((?:const\s+)?([\w:*&<>]+(?:\s*[\w:*&<>]+)*)\s+(\**&?\s*\w+)\s*(?:=|;|,))");
            std::smatch m;
            if (std::regex_search(trimmed, m, var_re)) {
                std::string declared_name = m[2].str();
                // Trim pointer/reference markers from the name
                size_t ns = declared_name.find_first_not_of("*& ");
                if (ns != std::string::npos) declared_name = declared_name.substr(ns);
                if (declared_name == symbol_name) {
                    std::string type = m[1].str();
                    if (!first) results += ",";
                    results += "{\"line\":" + std::to_string(line_num) +
                               ",\"type\":\"" + json_escape(type) +
                               "\",\"context\":\"variable\"}";
                    first = false;
                }
            }
            // Function return types
            std::regex func_re(R"(([\w:*&<>\s]+?)\s+(\w+)\s*\([^)]*\))");
            if (std::regex_search(trimmed, m, func_re) && m[2].str() == symbol_name) {
                std::string ret = m[1].str();
                size_t s = ret.find_first_not_of(" \t");
                if (s != std::string::npos) ret = ret.substr(s);
                if (!first) results += ",";
                results += "{\"line\":" + std::to_string(line_num) +
                           ",\"type\":\"" + json_escape(ret) +
                           "\",\"context\":\"return_type\"}";
                first = false;
            }
        }
        results += "]";
        return "{\"success\":true,\"results\":" + results + "}";
    }

    if (command == "inherit") {
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string file_path = json_string_field(params, "path");
        if (symbol_name.empty()) {
            return R"({"success":false,"error":"Missing 'symbol' parameter"})";
        }
        // Search in specified file or recursively
        auto search_inherit = [&](const std::string& search_path) -> std::string {
            if (!fs::is_regular_file(search_path, ec)) return "";
            auto content = FileIO::read(search_path);
            if (!content) return "";
            std::istringstream stream(content.value());
            std::string line;
            int line_num = 0;
            std::string results = "[";
            bool first = true;

            while (std::getline(stream, line)) {
                ++line_num;
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));

                // class X : public Y or struct X : public Y
                std::regex class_re(R"((?:class|struct)\s+(\w+)\s*:\s*(.+))");
                std::smatch m;
                if (std::regex_search(trimmed, m, class_re)) {
                    std::string cls_name = m[1].str();
                    std::string bases = m[2].str();
                    // Remove trailing {
                    size_t brace = bases.find('{');
                    if (brace != std::string::npos) bases = bases.substr(0, brace);

                    // Check if this class is a derived class of symbol_name (symbol_name is base)
                    if (bases.find(symbol_name) != std::string::npos) {
                        if (!first) results += ",";
                        results += "{\"derived\":\"" + json_escape(cls_name) +
                                   "\",\"base\":\"" + json_escape(symbol_name) +
                                   "\",\"line\":" + std::to_string(line_num) +
                                   ",\"file\":\"" + json_escape(search_path) + "\"}";
                        first = false;
                    }
                    // Check if this class IS symbol_name (symbol_name is derived)
                    if (cls_name == symbol_name) {
                        // Parse base classes
                        std::regex base_re(R"((?:public|private|protected)\s+(\w+))");
                        std::sregex_iterator it(bases.begin(), bases.end(), base_re);
                        std::sregex_iterator end;
                        for (; it != end; ++it) {
                            if (!first) results += ",";
                            results += "{\"derived\":\"" + json_escape(symbol_name) +
                                       "\",\"base\":\"" + (*it)[1].str() +
                                       "\",\"line\":" + std::to_string(line_num) +
                                       ",\"file\":\"" + json_escape(search_path) + "\"}";
                            first = false;
                        }
                    }
                }
            }
            results += "]";
            return results;
        };

        if (!file_path.empty()) {
            std::string r = search_inherit(file_path);
            if (r.empty()) return R"({"success":false,"error":"File not found"})";
            return "{\"success\":true,\"inheritance\":" + r + "}";
        }

        // Search recursively
        std::string root = json_string_field(params, "root");
        if (root.empty()) root = ".";
        std::string all = "[";
        bool first = true;
        for (auto& entry : fs::recursive_directory_iterator(
                root, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            if (ext != ".h" && ext != ".hpp" && ext != ".cpp") continue;
            std::string r = search_inherit(entry.path().string());
            if (r.size() > 2) { // more than just "[]"
                if (!first && r[1] != ']') all += ",";
                all += r.substr(1, r.size() - 2); // strip outer []
                first = false;
            }
        }
        all += "]";
        return "{\"success\":true,\"inheritance\":" + all + "}";
    }

    if (command == "hierarchy") {
        const std::string symbol_name = json_string_field(params, "symbol");
        const std::string root = json_string_field(params, "root");
        if (symbol_name.empty()) {
            return R"({"success":false,"error":"Missing 'symbol' parameter"})";
        }
        std::string search_root = root.empty() ? "." : root;

        // Build a map of class -> base classes
        std::map<std::string, std::vector<std::string>> bases;
        for (auto& entry : fs::recursive_directory_iterator(
                search_root, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            if (ext != ".h" && ext != ".hpp" && ext != ".cpp") continue;
            auto content = FileIO::read(entry.path().string());
            if (!content) continue;
            std::istringstream stream(content.value());
            std::string line;
            while (std::getline(stream, line)) {
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                std::regex class_re(R"((?:class|struct)\s+(\w+)\s*:\s*(.+))");
                std::smatch m;
                if (std::regex_search(trimmed, m, class_re)) {
                    std::string cls = m[1].str();
                    std::string base_list = m[2].str();
                    size_t brace = base_list.find('{');
                    if (brace != std::string::npos) base_list = base_list.substr(0, brace);
                    std::regex base_re(R"((?:public|private|protected)\s+(\w+))");
                    std::sregex_iterator it(base_list.begin(), base_list.end(), base_re);
                    std::sregex_iterator end;
                    for (; it != end; ++it) {
                        bases[cls].push_back((*it)[1].str());
                    }
                }
            }
        }

        // Walk up the hierarchy from symbol_name
        std::string chain = "[";
        std::vector<std::string> visited;
        std::string current = symbol_name;
        bool first_chain = true;
        while (bases.find(current) != bases.end()) {
            auto& b = bases[current];
            if (b.empty()) break;
            for (const auto& base : b) {
                if (!first_chain) chain += ",";
                chain += "{\"derived\":\"" + json_escape(current) + "\",\"base\":\"" + json_escape(base) + "\"}";
                first_chain = false;
                current = base;
            }
        }
        chain += "]";

        // Walk down (find derived classes)
        std::string derived_chain = "[";
        bool first_derived = true;
        for (auto& [cls, b] : bases) {
            for (const auto& base : b) {
                if (base == symbol_name) {
                    if (!first_derived) derived_chain += ",";
                    derived_chain += "{\"base\":\"" + json_escape(symbol_name) + "\",\"derived\":\"" + json_escape(cls) + "\"}";
                    first_derived = false;
                }
            }
        }
        derived_chain += "]";

        return "{\"success\":true,\"bases\":" + chain + ",\"derived\":" + derived_chain + "}";
    }

    return error_response(command);
}
