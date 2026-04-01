#include "JsonUtils.h"

#include <cctype>

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) out += ' ';
                else out.push_back(static_cast<char>(c));
                break;
        }
    }
    return out;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::string json_unescape_string(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c != '\\' || i + 1 >= value.size()) {
            out.push_back(c);
            continue;
        }

        char n = value[++i];
        switch (n) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (i + 4 >= value.size()) break;
                int cp = 0;
                bool ok = true;
                for (int j = 0; j < 4; ++j) {
                    int hv = hex_value(value[i + 1 + j]);
                    if (hv < 0) { ok = false; break; }
                    cp = (cp << 4) | hv;
                }
                i += 4;
                if (!ok) break;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < value.size() && value[i + 1] == '\\' && value[i + 2] == 'u') {
                    int low = 0;
                    bool low_ok = true;
                    for (int j = 0; j < 4; ++j) {
                        int hv = hex_value(value[i + 3 + j]);
                        if (hv < 0) { low_ok = false; break; }
                        low = (low << 4) | hv;
                    }
                    if (low_ok && low >= 0xDC00 && low <= 0xDFFF) {
                        uint32_t full = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                        out.push_back(static_cast<char>(0xF0 | ((full >> 18) & 0x07)));
                        out.push_back(static_cast<char>(0x80 | ((full >> 12) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | ((full >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (full & 0x3F)));
                        i += 6;
                        break;
                    }
                }

                if (cp < 0x80) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default: out.push_back(n); break;
        }
    }
    return out;
}

static std::string json_field_impl(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = body.find('"', pos);
    if (pos == std::string::npos) return "";
    size_t end = pos + 1;
    while (end < body.size()) {
        if (body[end] == '"' && body[end - 1] != '\\') break;
        ++end;
    }
    if (end >= body.size()) return "";
    return json_unescape_string(body.substr(pos + 1, end - pos - 1));
}

std::string json_string_field(const std::string& body, const std::string& key) {
    return json_field_impl(body, key);
}

std::string json_number_field(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    size_t end = pos;
    while (end < body.size() && (std::isdigit(static_cast<unsigned char>(body[end])) || body[end] == '-')) ++end;
    return body.substr(pos, end - pos);
}

std::string json_field(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    ++pos;

    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;

    if (body[pos] == '"') {
        size_t end = pos + 1;
        while (end < body.size()) {
            if (body[end] == '"' && body[end - 1] != '\\') break;
            ++end;
        }
        return body.substr(pos, end - pos + 1);
    } else if (body[pos] == '{' || body[pos] == '[') {
        char start_char = body[pos];
        char end_char = (start_char == '{') ? '}' : ']';
        int depth = 1;
        size_t end = pos + 1;
        while (end < body.size() && depth > 0) {
            if (body[end] == start_char) {
                ++depth;
            } else if (body[end] == end_char) {
                --depth;
            } else if (body[end] == '"' && body[end - 1] != '\\') {
                ++end;
                while (end < body.size() && body[end] != '"') {
                    if (body[end] == '\\' && end + 1 < body.size()) {
                        end += 2;
                    } else {
                        ++end;
                    }
                }
            }
            ++end;
        }
        if (depth == 0) {
            return body.substr(pos, end - pos);
        }
        return "";
    } else {
        size_t end = pos;
        while (end < body.size() && body[end] != ',' && body[end] != '}' && !std::isspace(static_cast<unsigned char>(body[end]))) {
            ++end;
        }
        return body.substr(pos, end - pos);
    }
}

bool json_bool_field(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;

    if (body.compare(pos, 4, "true") == 0) {
        return true;
    } else if (body.compare(pos, 5, "false") == 0) {
        return false;
    }
    return false;
}

std::string json_method(const std::string& body) { return json_string_field(body, "method"); }
std::string json_id(const std::string& body) {
    std::string id = json_number_field(body, "id");
    if (!id.empty()) return id;
    return json_string_field(body, "id");
}
