#pragma once

#include <string>

std::string json_escape(const std::string& value);
std::string json_unescape_string(const std::string& value);
std::string json_string_field(const std::string& body, const std::string& key);
std::string json_number_field(const std::string& body, const std::string& key);
std::string json_field(const std::string& body, const std::string& key);
bool json_bool_field(const std::string& body, const std::string& key);
std::string json_method(const std::string& body);
std::string json_id(const std::string& body);
