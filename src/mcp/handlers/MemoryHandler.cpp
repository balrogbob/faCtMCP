#include "MemoryHandler.h"
#include "../JsonUtils.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <set>

namespace {

struct Entity {
    std::string name;
    std::string entity_type;
    std::vector<std::string> observations;
};

struct Relation {
    std::string from;
    std::string to;
    std::string relation_type;
};

struct KnowledgeGraph {
    std::vector<Entity> entities;
    std::vector<Relation> relations;
};

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return result;
}

bool contains_ignore_case(const std::string& haystack, const std::string& needle) {
    return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

std::string get_memory_file_path() {
    const char* env = getenv("MEMORY_FILE_PATH");
    if (env && std::string(env).length() > 0) {
        return std::string(env);
    }
    return "memory.json";
}

std::vector<std::string> extract_json_array(const std::string& body, const std::string& key) {
    std::vector<std::string> items;
    
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return items;
    
    pos = body.find(':', pos);
    if (pos == std::string::npos) return items;
    
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    
    if (pos >= body.size() || body[pos] != '[') return items;
    
    int bracket_count = 1;
    size_t start = pos + 1;
    
    for (size_t i = start; i < body.size() && bracket_count > 0; ++i) {
        if (body[i] == '[') bracket_count++;
        else if (body[i] == ']') bracket_count--;
        
        if (bracket_count == 0) {
            size_t end = i;
            size_t item_start = start;
            
            while (item_start < end) {
                while (item_start < end && std::isspace(static_cast<unsigned char>(body[item_start]))) ++item_start;
                
                if (item_start >= end) break;
                
                if (body[item_start] == '{') {
                    int obj_brackets = 1;
                    size_t obj_start = item_start;
                    for (size_t j = obj_start + 1; j < end && obj_brackets > 0; ++j) {
                        if (body[j] == '{') obj_brackets++;
                        else if (body[j] == '}') obj_brackets--;
                        
                        if (obj_brackets == 0) {
                            items.push_back(body.substr(obj_start, j - obj_start + 1));
                            item_start = j + 1;
                            break;
                        }
                    }
                } else if (body[item_start] == '"') {
                    size_t str_end = item_start + 1;
                    while (str_end < end && body[str_end] != '"') {
                        if (body[str_end] == '\\') str_end++;
                        str_end++;
                    }
                    if (str_end < end) {
                        items.push_back(body.substr(item_start + 1, str_end - item_start - 1));
                        item_start = str_end + 1;
                    } else {
                        item_start++;
                    }
                } else {
                    item_start++;
                }
                
                while (item_start < end && body[item_start] != ',') item_start++;
                if (body[item_start] == ',') item_start++;
            }
            break;
        }
    }
    
    return items;
}

Entity parse_entity(const std::string& entity_str) {
    Entity entity;
    entity.name = json_string_field(entity_str, "name");
    entity.entity_type = json_string_field(entity_str, "entityType");
    
    std::vector<std::string> obs_array = extract_json_array(entity_str, "observations");
    for (const auto& obs : obs_array) {
        if (!obs.empty()) {
            entity.observations.push_back(obs);
        }
    }
    
    return entity;
}

Relation parse_relation(const std::string& relation_str) {
    Relation relation;
    relation.from = json_string_field(relation_str, "from");
    relation.to = json_string_field(relation_str, "to");
    relation.relation_type = json_string_field(relation_str, "relationType");
    return relation;
}

class KnowledgeGraphManager {
private:
    std::string file_path_;
    
    KnowledgeGraph load_graph() {
        KnowledgeGraph graph;
        std::ifstream file(file_path_);
        
        if (!file.is_open()) {
            return graph;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || std::all_of(line.begin(), line.end(), 
                [](unsigned char c){ return std::isspace(c); })) {
                continue;
            }
            
            std::string type = json_string_field(line, "type");
            if (type == "entity") {
                Entity entity = parse_entity(line);
                if (!entity.name.empty()) {
                    graph.entities.push_back(entity);
                }
            } else if (type == "relation") {
                Relation relation = parse_relation(line);
                if (!relation.from.empty() && !relation.to.empty()) {
                    graph.relations.push_back(relation);
                }
            }
        }
        
        file.close();
        return graph;
    }
    
    void save_graph(const KnowledgeGraph& graph) {
        std::ofstream file(file_path_);
        
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open memory file for writing");
        }
        
        for (const auto& entity : graph.entities) {
            file << "{\"type\":\"entity\",\"name\":\"" << json_escape(entity.name) 
                 << "\",\"entityType\":\"" << json_escape(entity.entity_type) 
                 << "\",\"observations\":[";
            
            for (size_t i = 0; i < entity.observations.size(); ++i) {
                if (i > 0) file << ",";
                file << "\"" << json_escape(entity.observations[i]) << "\"";
            }
            
            file << "]}\n";
        }
        
        for (const auto& relation : graph.relations) {
            file << "{\"type\":\"relation\",\"from\":\"" << json_escape(relation.from)
                 << "\",\"to\":\"" << json_escape(relation.to)
                 << "\",\"relationType\":\"" << json_escape(relation.relation_type) << "\"}\n";
        }
        
        file.close();
    }
    
public:
    KnowledgeGraphManager() : file_path_(get_memory_file_path()) {}
    
    std::vector<Entity> create_entities(const std::vector<Entity>& entities) {
        KnowledgeGraph graph = load_graph();
        std::vector<Entity> new_entities;
        
        for (const auto& entity : entities) {
            bool exists = false;
            for (const auto& existing : graph.entities) {
                if (existing.name == entity.name) {
                    exists = true;
                    break;
                }
            }
            
            if (!exists && !entity.name.empty()) {
                graph.entities.push_back(entity);
                new_entities.push_back(entity);
            }
        }
        
        save_graph(graph);
        return new_entities;
    }
    
    std::vector<Relation> create_relations(const std::vector<Relation>& relations) {
        KnowledgeGraph graph = load_graph();
        std::vector<Relation> new_relations;
        
        for (const auto& relation : relations) {
            bool exists = false;
            for (const auto& existing : graph.relations) {
                if (existing.from == relation.from && 
                    existing.to == relation.to && 
                    existing.relation_type == relation.relation_type) {
                    exists = true;
                    break;
                }
            }
            
            if (!exists && !relation.from.empty() && !relation.to.empty()) {
                graph.relations.push_back(relation);
                new_relations.push_back(relation);
            }
        }
        
        save_graph(graph);
        return new_relations;
    }
    
    struct AddObservationResult {
        std::string entity_name;
        std::vector<std::string> added_observations;
    };
    
    std::vector<AddObservationResult> add_observations(
        const std::vector<std::pair<std::string, std::vector<std::string>>>& observations) {
        
        KnowledgeGraph graph = load_graph();
        std::vector<AddObservationResult> results;
        
        for (const auto& obs : observations) {
            AddObservationResult result;
            result.entity_name = obs.first;
            
            Entity* entity = nullptr;
            for (auto& e : graph.entities) {
                if (e.name == obs.first) {
                    entity = &e;
                    break;
                }
            }
            
            if (!entity) {
                throw std::runtime_error("Entity with name " + obs.first + " not found");
            }
            
            for (const auto& content : obs.second) {
                bool exists = false;
                for (const auto& existing : entity->observations) {
                    if (existing == content) {
                        exists = true;
                        break;
                    }
                }
                
                if (!exists) {
                    entity->observations.push_back(content);
                    result.added_observations.push_back(content);
                }
            }
            
            results.push_back(result);
        }
        
        save_graph(graph);
        return results;
    }
    
    void delete_entities(const std::vector<std::string>& entity_names) {
        KnowledgeGraph graph = load_graph();
        
        std::set<std::string> names_set(entity_names.begin(), entity_names.end());
        
        graph.entities.erase(
            std::remove_if(graph.entities.begin(), graph.entities.end(),
                [&names_set](const Entity& e) {
                    return names_set.find(e.name) != names_set.end();
                }),
            graph.entities.end()
        );
        
        graph.relations.erase(
            std::remove_if(graph.relations.begin(), graph.relations.end(),
                [&names_set](const Relation& r) {
                    return names_set.find(r.from) != names_set.end() || 
                           names_set.find(r.to) != names_set.end();
                }),
            graph.relations.end()
        );
        
        save_graph(graph);
    }
    
    void delete_observations(
        const std::vector<std::pair<std::string, std::vector<std::string>>>& deletions) {
        
        KnowledgeGraph graph = load_graph();
        
        for (const auto& deletion : deletions) {
            for (auto& entity : graph.entities) {
                if (entity.name == deletion.first) {
                    std::set<std::string> obs_set(deletion.second.begin(), deletion.second.end());
                    
                    entity.observations.erase(
                        std::remove_if(entity.observations.begin(), entity.observations.end(),
                            [&obs_set](const std::string& obs) {
                                return obs_set.find(obs) != obs_set.end();
                            }),
                        entity.observations.end()
                    );
                }
            }
        }
        
        save_graph(graph);
    }
    
    void delete_relations(const std::vector<Relation>& relations) {
        KnowledgeGraph graph = load_graph();
        
        graph.relations.erase(
            std::remove_if(graph.relations.begin(), graph.relations.end(),
                [&relations](const Relation& r) {
                    for (const auto& del : relations) {
                        if (r.from == del.from && 
                            r.to == del.to && 
                            r.relation_type == del.relation_type) {
                            return true;
                        }
                    }
                    return false;
                }),
            graph.relations.end()
        );
        
        save_graph(graph);
    }
    
    KnowledgeGraph read_graph() {
        return load_graph();
    }
    
    KnowledgeGraph search_nodes(const std::string& query) {
        KnowledgeGraph graph = load_graph();
        KnowledgeGraph result;
        
        for (const auto& entity : graph.entities) {
            bool match = contains_ignore_case(entity.name, query) ||
                        contains_ignore_case(entity.entity_type, query);
            
            if (!match) {
                for (const auto& obs : entity.observations) {
                    if (contains_ignore_case(obs, query)) {
                        match = true;
                        break;
                    }
                }
            }
            
            if (match) {
                result.entities.push_back(entity);
            }
        }
        
        std::set<std::string> matched_names;
        for (const auto& entity : result.entities) {
            matched_names.insert(entity.name);
        }
        
        for (const auto& relation : graph.relations) {
            if (matched_names.find(relation.from) != matched_names.end() &&
                matched_names.find(relation.to) != matched_names.end()) {
                result.relations.push_back(relation);
            }
        }
        
        return result;
    }
    
    KnowledgeGraph open_nodes(const std::vector<std::string>& names) {
        KnowledgeGraph graph = load_graph();
        KnowledgeGraph result;
        
        std::set<std::string> names_set(names.begin(), names.end());
        
        for (const auto& entity : graph.entities) {
            if (names_set.find(entity.name) != names_set.end()) {
                result.entities.push_back(entity);
            }
        }
        
        for (const auto& relation : graph.relations) {
            if (names_set.find(relation.from) != names_set.end() &&
                names_set.find(relation.to) != names_set.end()) {
                result.relations.push_back(relation);
            }
        }
        
        return result;
    }
};

std::string entity_to_json(const Entity& entity) {
    std::ostringstream oss;
    oss << "{\"name\":\"" << json_escape(entity.name) << "\","
        << "\"entityType\":\"" << json_escape(entity.entity_type) << "\","
        << "\"observations\":[";
    
    for (size_t i = 0; i < entity.observations.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << json_escape(entity.observations[i]) << "\"";
    }
    
    oss << "]}";
    return oss.str();
}

std::string relation_to_json(const Relation& relation) {
    std::ostringstream oss;
    oss << "{\"from\":\"" << json_escape(relation.from) << "\","
        << "\"to\":\"" << json_escape(relation.to) << "\","
        << "\"relationType\":\"" << json_escape(relation.relation_type) << "\"}";
    return oss.str();
}

std::string graph_to_json(const KnowledgeGraph& graph) {
    std::ostringstream oss;
    oss << "{\"entities\":[";
    
    for (size_t i = 0; i < graph.entities.size(); ++i) {
        if (i > 0) oss << ",";
        oss << entity_to_json(graph.entities[i]);
    }
    
    oss << "],\"relations\":[";
    
    for (size_t i = 0; i < graph.relations.size(); ++i) {
        if (i > 0) oss << ",";
        oss << relation_to_json(graph.relations[i]);
    }
    
    oss << "]}";
    return oss.str();
}

KnowledgeGraphManager& get_manager() {
    static KnowledgeGraphManager manager;
    return manager;
}

} // namespace

std::string handle_create_entities(const std::string& params) {
    try {
        std::vector<std::string> entity_strs = extract_json_array(params, "entities");
        std::vector<Entity> entities;
        
        for (const auto& es : entity_strs) {
            Entity entity = parse_entity(es);
            if (!entity.name.empty()) {
                entities.push_back(entity);
            }
        }
        
        auto new_entities = get_manager().create_entities(entities);
        
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < new_entities.size(); ++i) {
            if (i > 0) oss << ",";
            oss << entity_to_json(new_entities[i]);
        }
        oss << "]";
        
        return R"({"success":true,"result":)" + oss.str() + "}";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}

std::string handle_create_relations(const std::string& params) {
    try {
        std::vector<std::string> relation_strs = extract_json_array(params, "relations");
        std::vector<Relation> relations;
        
        for (const auto& rs : relation_strs) {
            Relation relation = parse_relation(rs);
            if (!relation.from.empty() && !relation.to.empty()) {
                relations.push_back(relation);
            }
        }
        
        auto new_relations = get_manager().create_relations(relations);
        
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < new_relations.size(); ++i) {
            if (i > 0) oss << ",";
            oss << relation_to_json(new_relations[i]);
        }
        oss << "]";
        
        return R"({"success":true,"result":)" + oss.str() + "}";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}

std::string handle_add_observations(const std::string& params) {
    try {
        std::vector<std::string> obs_strs = extract_json_array(params, "observations");
        std::vector<std::pair<std::string, std::vector<std::string>>> observations;
        
        for (const auto& os : obs_strs) {
            std::string entity_name = json_string_field(os, "entityName");
            if (entity_name.empty()) continue;
            
            std::vector<std::string> contents;
            std::vector<std::string> content_strs = extract_json_array(os, "contents");
            for (const auto& cs : content_strs) {
                if (!cs.empty()) {
                    contents.push_back(cs);
                }
            }
            
            observations.push_back({entity_name, contents});
        }
        
        auto results = get_manager().add_observations(observations);
        
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"entityName\":\"" << json_escape(results[i].entity_name) << "\","
                << "\"addedObservations\":[";
            for (size_t j = 0; j < results[i].added_observations.size(); ++j) {
                if (j > 0) oss << ",";
                oss << "\"" << json_escape(results[i].added_observations[j]) << "\"";
            }
            oss << "]}";
        }
        oss << "]";
        
        return R"({"success":true,"result":)" + oss.str() + "}";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}

std::string handle_delete_entities(const std::string& params) {
    try {
        std::vector<std::string> entity_names = extract_json_array(params, "entityNames");
        get_manager().delete_entities(entity_names);
        
        return R"({"success":true,"result":"Entities deleted successfully"})";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}

std::string handle_delete_observations(const std::string& params) {
    try {
        std::vector<std::string> deletion_strs = extract_json_array(params, "deletions");
        std::vector<std::pair<std::string, std::vector<std::string>>> deletions;
        
        for (const auto& ds : deletion_strs) {
            std::string entity_name = json_string_field(ds, "entityName");
            if (entity_name.empty()) continue;
            
            std::vector<std::string> observations;
            std::vector<std::string> obs_strs = extract_json_array(ds, "observations");
            for (const auto& os : obs_strs) {
                if (!os.empty()) {
                    observations.push_back(os);
                }
            }
            
            deletions.push_back({entity_name, observations});
        }
        
        get_manager().delete_observations(deletions);
        
        return R"({"success":true,"result":"Observations deleted successfully"})";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}

std::string handle_delete_relations(const std::string& params) {
    try {
        std::vector<std::string> relation_strs = extract_json_array(params, "relations");
        std::vector<Relation> relations;
        
        for (const auto& rs : relation_strs) {
            Relation relation = parse_relation(rs);
            relations.push_back(relation);
        }
        
        get_manager().delete_relations(relations);
        
        return R"({"success":true,"result":"Relations deleted successfully"})";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}

std::string handle_read_graph(const std::string& params) {
    try {
        auto graph = get_manager().read_graph();
        return R"({"success":true,"result":)" + graph_to_json(graph) + "}";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}

std::string handle_search_nodes(const std::string& params) {
    try {
        std::string query = json_string_field(params, "query");
        if (query.empty()) {
            throw std::runtime_error("Query is required");
        }
        
        auto graph = get_manager().search_nodes(query);
        return R"({"success":true,"result":)" + graph_to_json(graph) + "}";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}

std::string handle_open_nodes(const std::string& params) {
    try {
        std::vector<std::string> names = extract_json_array(params, "names");
        auto graph = get_manager().open_nodes(names);
        return R"({"success":true,"result":)" + graph_to_json(graph) + "}";
        
    } catch (const std::exception& e) {
        return R"({"success":false,"error":")" + json_escape(e.what()) + R"("})";
    }
}
