#pragma once
#include <string>

// Memory tool handlers for knowledge graph operations
// Supports entities, relations, and observations with JSON file storage

// Tool handlers
std::string handle_create_entities(const std::string& params);
std::string handle_create_relations(const std::string& params);
std::string handle_add_observations(const std::string& params);
std::string handle_delete_entities(const std::string& params);
std::string handle_delete_observations(const std::string& params);
std::string handle_delete_relations(const std::string& params);
std::string handle_read_graph(const std::string& params);
std::string handle_search_nodes(const std::string& params);
std::string handle_open_nodes(const std::string& params);
