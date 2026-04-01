#pragma once
#include <string>
#include <optional>

class FileIO {
public:
    // Read entire file into string
    static std::optional<std::string> read(const std::string& path);
    
    // Write text to file (overwrites)
    static bool write(const std::string& path, const std::string& content);
    
    // Normalize path (e.g., handle both / and \)
    static std::string normalize_path(const std::string& path);
};
