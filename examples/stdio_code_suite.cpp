#include "example_server_utils.h"

#include "mcp/handlers/AnalyzeHandler.h"
#include "mcp/handlers/GitHandler.h"
#include "mcp/handlers/MemoryHandler.h"
#include "mcp/handlers/ProjectHandler.h"
#include "mcp/handlers/RefactorHandler.h"
#include "mcp/handlers/SymbolsHandler.h"

#include <string>

namespace {

const char* kSymbolsJson = R"JSON({"name":"symbols","description":"Code navigation. Commands: get, def, ref, signature, doc, type, inherit, hierarchy.","inputSchema":{"type":"object","properties":{"command":{"type":"string"},"path":{"type":"string"},"symbol":{"type":"string"}},"required":["command"]}})JSON";
const char* kGitJson = R"JSON({"name":"git","description":"Version control queries. Commands: diff, status, log, blame, branch, commit_files.","inputSchema":{"type":"object","properties":{"command":{"type":"string"},"path":{"type":"string"},"staged":{"type":"string"},"file":{"type":"string"},"max":{"type":"number"},"hash":{"type":"string"}},"required":["command"]}})JSON";
const char* kProjectJson = R"JSON({"name":"project","description":"File and workspace structure. Commands: list, info, tree, root, config, diff, recent, workspace.","inputSchema":{"type":"object","properties":{"command":{"type":"string"},"path":{"type":"string"},"file_a":{"type":"string"},"file_b":{"type":"string"},"max_depth":{"type":"number"}},"required":["command"]}})JSON";
const char* kAnalyzeJson = R"JSON({"name":"analyze","description":"Code analysis. Commands: complexity, dead_code, unused, cycles, long, duplicate, naming, ast, preprocessor, include_graph.","inputSchema":{"type":"object","properties":{"command":{"type":"string"},"path":{"type":"string"},"threshold":{"type":"number"}},"required":["command"]}})JSON";
const char* kRefactorJson = R"JSON({"name":"refactor","description":"Code transformation previews. Commands: extract, variable, inline, signature, move_file, split, merge.","inputSchema":{"type":"object","properties":{"command":{"type":"string"}},"required":["command"]}})JSON";
const char* kCreateEntitiesJson = R"JSON({"name":"create_entities","description":"Create multiple new entities in the knowledge graph","inputSchema":{"type":"object","properties":{"entities":{"type":"array"}},"required":["entities"]}})JSON";
const char* kCreateRelationsJson = R"JSON({"name":"create_relations","description":"Create multiple new relations between entities in the knowledge graph","inputSchema":{"type":"object","properties":{"relations":{"type":"array"}},"required":["relations"]}})JSON";
const char* kAddObservationsJson = R"JSON({"name":"add_observations","description":"Add new observations to existing entities in the knowledge graph","inputSchema":{"type":"object","properties":{"observations":{"type":"array"}},"required":["observations"]}})JSON";
const char* kDeleteEntitiesJson = R"JSON({"name":"delete_entities","description":"Delete multiple entities and their associated relations from the knowledge graph","inputSchema":{"type":"object","properties":{"entityNames":{"type":"array"}},"required":["entityNames"]}})JSON";
const char* kDeleteObservationsJson = R"JSON({"name":"delete_observations","description":"Delete specific observations from entities in the knowledge graph","inputSchema":{"type":"object","properties":{"deletions":{"type":"array"}},"required":["deletions"]}})JSON";
const char* kDeleteRelationsJson = R"JSON({"name":"delete_relations","description":"Delete multiple relations from the knowledge graph","inputSchema":{"type":"object","properties":{"relations":{"type":"array"}},"required":["relations"]}})JSON";
const char* kReadGraphJson = R"JSON({"name":"read_graph","description":"Read the entire knowledge graph","inputSchema":{"type":"object","properties":{}}})JSON";
const char* kSearchNodesJson = R"JSON({"name":"search_nodes","description":"Search for nodes in the knowledge graph based on a query","inputSchema":{"type":"object","properties":{"query":{"type":"string"}},"required":["query"]}})JSON";
const char* kOpenNodesJson = R"JSON({"name":"open_nodes","description":"Open specific nodes in the knowledge graph by their names","inputSchema":{"type":"object","properties":{"names":{"type":"array"}},"required":["names"]}})JSON";

const char* wrap_result(const char* params, std::string (*handler)(const std::string&)) {
    static thread_local std::string result;
    result = handler(params ? std::string(params) : std::string("{}"));
    return result.c_str();
}

const char* symbols_handler(const char* params, void*) { return wrap_result(params, handle_symbols); }
const char* git_handler(const char* params, void*) { return wrap_result(params, handle_git); }
const char* project_handler(const char* params, void*) { return wrap_result(params, handle_project); }
const char* analyze_handler(const char* params, void*) { return wrap_result(params, handle_analyze); }
const char* refactor_handler(const char* params, void*) { return wrap_result(params, handle_refactor); }
const char* create_entities_handler(const char* params, void*) { return wrap_result(params, handle_create_entities); }
const char* create_relations_handler(const char* params, void*) { return wrap_result(params, handle_create_relations); }
const char* add_observations_handler(const char* params, void*) { return wrap_result(params, handle_add_observations); }
const char* delete_entities_handler(const char* params, void*) { return wrap_result(params, handle_delete_entities); }
const char* delete_observations_handler(const char* params, void*) { return wrap_result(params, handle_delete_observations); }
const char* delete_relations_handler(const char* params, void*) { return wrap_result(params, handle_delete_relations); }
const char* read_graph_handler(const char* params, void*) { return wrap_result(params, handle_read_graph); }
const char* search_nodes_handler(const char* params, void*) { return wrap_result(params, handle_search_nodes); }
const char* open_nodes_handler(const char* params, void*) { return wrap_result(params, handle_open_nodes); }

} // namespace

int main() {
    const ExampleToolRegistration registrations[] = {
        {"symbols", kSymbolsJson, symbols_handler, nullptr},
        {"git", kGitJson, git_handler, nullptr},
        {"project", kProjectJson, project_handler, nullptr},
        {"analyze", kAnalyzeJson, analyze_handler, nullptr},
        {"refactor", kRefactorJson, refactor_handler, nullptr},
        {"create_entities", kCreateEntitiesJson, create_entities_handler, nullptr},
        {"create_relations", kCreateRelationsJson, create_relations_handler, nullptr},
        {"add_observations", kAddObservationsJson, add_observations_handler, nullptr},
        {"delete_entities", kDeleteEntitiesJson, delete_entities_handler, nullptr},
        {"delete_observations", kDeleteObservationsJson, delete_observations_handler, nullptr},
        {"delete_relations", kDeleteRelationsJson, delete_relations_handler, nullptr},
        {"read_graph", kReadGraphJson, read_graph_handler, nullptr},
        {"search_nodes", kSearchNodesJson, search_nodes_handler, nullptr},
        {"open_nodes", kOpenNodesJson, open_nodes_handler, nullptr},
    };
    return run_example_stdio_server(registrations, static_cast<int>(sizeof(registrations) / sizeof(registrations[0])));
}
