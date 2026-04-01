#include "example_server_utils.h"

#include "mcp/handlers/FilesystemHandlers.h"
#include "mcp/handlers/GlobHandler.h"
#include "mcp/handlers/GrepHandler.h"

#include <string>

namespace {

const char* kCreateDirectoryJson = R"JSON({"name":"create_directory","description":"Create a directory path, including parents, and return the resolved path.","inputSchema":{"type":"object","properties":{"path":{"type":"string","description":"Directory path to create"}},"required":["path"]}})JSON";
const char* kListDirectoryJson = R"JSON({"name":"list_directory","description":"List files and directories directly inside a path.","inputSchema":{"type":"object","properties":{"path":{"type":"string","description":"Directory path to list"}},"required":["path"]}})JSON";
const char* kListDirectoryWithSizesJson = R"JSON({"name":"list_directory_with_sizes","description":"List a directory with file sizes and modification times.","inputSchema":{"type":"object","properties":{"path":{"type":"string","description":"Directory path to list"},"sortBy":{"type":"string","enum":["name","size"],"description":"Sort entries by name or size"}},"required":["path"]}})JSON";
const char* kDirectoryTreeJson = R"JSON({"name":"directory_tree","description":"Recursively build a tree of files and directories.","inputSchema":{"type":"object","properties":{"path":{"type":"string","description":"Root directory to inspect"}},"required":["path"]}})JSON";
const char* kMoveFileJson = R"JSON({"name":"move_file","description":"Move or rename a file or directory.","inputSchema":{"type":"object","properties":{"source":{"type":"string","description":"Source path"},"destination":{"type":"string","description":"Destination path"}},"required":["source","destination"]}})JSON";
const char* kSearchFilesJson = R"JSON({"name":"search_files","description":"Recursively search for matching files and directories by name.","inputSchema":{"type":"object","properties":{"path":{"type":"string","description":"Root directory to search"},"pattern":{"type":"string","description":"Case-insensitive name fragment to search for"},"excludePatterns":{"type":"array","items":{"type":"string"},"description":"Optional exclude patterns"}},"required":["path","pattern"]}})JSON";
const char* kGetFileInfoJson = R"JSON({"name":"get_file_info","description":"Return metadata for a file or directory.","inputSchema":{"type":"object","properties":{"path":{"type":"string","description":"Path to inspect"}},"required":["path"]}})JSON";
const char* kGlobFilesJson = R"JSON({"name":"glob_files","description":"Find files matching a POSIX-like glob pattern. Supports * (any chars in segment), ? (single char), and ** (recursive directory traversal). Returns relative paths sorted alphabetically. Limited to 500 results.","inputSchema":{"type":"object","properties":{"pattern":{"type":"string","description":"Glob pattern, e.g. '**/*.cpp', '*.h', 'src/**/*.txt'"},"path":{"type":"string","description":"Root directory to search from. Defaults to current directory."}},"required":["pattern"]}})JSON";
const char* kGrepFilesJson = R"JSON({"name":"grep_files","description":"Search file contents using a regex pattern across all files in a directory tree. Skips binary files automatically. Returns file path, line number, and matching line text for each hit.","inputSchema":{"type":"object","properties":{"pattern":{"type":"string","description":"Regular expression pattern to search for"},"path":{"type":"string","description":"Root directory to search. Defaults to current directory."},"include":{"type":"string","description":"Optional filename glob filter"},"exclude":{"type":"string","description":"Optional exclude glob list"},"max_results":{"type":"number","description":"Maximum matches to return"}},"required":["pattern"]}})JSON";

const char* wrap_result(const char* params, std::string (*handler)(const std::string&)) {
    static thread_local std::string result;
    result = handler(params ? std::string(params) : std::string("{}"));
    return result.c_str();
}

const char* create_directory_handler(const char* params, void*) { return wrap_result(params, handle_create_directory); }
const char* list_directory_handler(const char* params, void*) { return wrap_result(params, handle_list_directory); }
const char* list_directory_with_sizes_handler(const char* params, void*) { return wrap_result(params, handle_list_directory_with_sizes); }
const char* directory_tree_handler(const char* params, void*) { return wrap_result(params, handle_directory_tree); }
const char* move_file_handler(const char* params, void*) { return wrap_result(params, handle_move_file_fs); }
const char* search_files_handler(const char* params, void*) { return wrap_result(params, handle_search_files); }
const char* get_file_info_handler(const char* params, void*) { return wrap_result(params, handle_get_file_info); }
const char* glob_files_handler(const char* params, void*) { return wrap_result(params, handle_glob_files); }
const char* grep_files_handler(const char* params, void*) { return wrap_result(params, handle_grep_files); }

} // namespace

int main() {
    const ExampleToolRegistration registrations[] = {
        {"create_directory", kCreateDirectoryJson, create_directory_handler, nullptr},
        {"list_directory", kListDirectoryJson, list_directory_handler, nullptr},
        {"list_directory_with_sizes", kListDirectoryWithSizesJson, list_directory_with_sizes_handler, nullptr},
        {"directory_tree", kDirectoryTreeJson, directory_tree_handler, nullptr},
        {"move_file", kMoveFileJson, move_file_handler, nullptr},
        {"search_files", kSearchFilesJson, search_files_handler, nullptr},
        {"get_file_info", kGetFileInfoJson, get_file_info_handler, nullptr},
        {"glob_files", kGlobFilesJson, glob_files_handler, nullptr},
        {"grep_files", kGrepFilesJson, grep_files_handler, nullptr},
    };
    return run_example_stdio_server(registrations, static_cast<int>(sizeof(registrations) / sizeof(registrations[0])));
}
