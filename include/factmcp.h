#pragma once

#include <stddef.h>

#ifdef _WIN32
  #ifdef FACTMCP_EXPORTS
    #define FACTMCP_API __declspec(dllexport)
  #elif defined(FACTMCP_SHARED)
    #define FACTMCP_API __declspec(dllimport)
  #else
    #define FACTMCP_API
  #endif
#else
  #define FACTMCP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct factmcp_server factmcp_server;

typedef enum factmcp_transport {
    FACTMCP_TRANSPORT_HTTP = 0,
    FACTMCP_TRANSPORT_STDIO = 1
} factmcp_transport;

typedef const char* (*factmcp_tool_handler)(const char* json_params, void* user_data);

FACTMCP_API factmcp_server* factmcp_create(factmcp_transport transport, int port);
FACTMCP_API void factmcp_destroy(factmcp_server* server);
FACTMCP_API int factmcp_start(factmcp_server* server);
FACTMCP_API void factmcp_stop(factmcp_server* server);
FACTMCP_API void factmcp_run(factmcp_server* server);
FACTMCP_API int factmcp_port(const factmcp_server* server);

FACTMCP_API int factmcp_register_tool(
    factmcp_server* server,
    const char* name,
    const char* tool_json,
    factmcp_tool_handler handler,
    void* user_data);

FACTMCP_API const char* factmcp_process_jsonrpc(factmcp_server* server, const char* json_body);
FACTMCP_API void factmcp_free_string(const char* text);

#ifdef __cplusplus
}
#endif
