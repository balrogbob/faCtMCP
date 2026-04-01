#pragma once

#include <string>

std::string handle_create_directory(const std::string& params);
std::string handle_list_directory(const std::string& params);
std::string handle_list_directory_with_sizes(const std::string& params);
std::string handle_directory_tree(const std::string& params);
std::string handle_move_file_fs(const std::string& params);
std::string handle_search_files(const std::string& params);
std::string handle_get_file_info(const std::string& params);
