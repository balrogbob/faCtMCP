#include "FileIO.h"
#include <fstream>
#include <filesystem>
#include <algorithm>

void invalidate_grep_cache();

namespace fs = std::filesystem;

std::optional<std::string> FileIO::read(const std::string& path) {
    auto normalized = normalize_path(path);
    
    std::ifstream file(normalized, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;
    
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::string buffer(size, '\0');
    if (!file.read(buffer.data(), static_cast<std::streamsize>(size))) {
        return std::nullopt;
    }
    
    return buffer;
}

bool FileIO::write(const std::string& path, const std::string& content) {
    auto normalized = normalize_path(path);
    
    // Ensure parent directory exists
    fs::path p(normalized);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }
    
    std::ofstream file(normalized, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    const bool ok = !file.fail();
    if (ok) {
        invalidate_grep_cache();
    }
    return ok;
}

std::string FileIO::normalize_path(const std::string& path) {
    // For now, just ensure forward slashes (Windows will accept them)
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    
    // Convert to absolute path
    return fs::absolute(fs::path(result)).string();
}
