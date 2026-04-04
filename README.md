# faCtMCP

`faCtMCP` is a standalone C++ MCP server core with a C ABI, intended to be downloaded and embedded by developers building agent tools.

It is meant to be embedded into other applications so they can expose custom MCP tools without having to reimplement transport, JSON-RPC framing, or tool dispatch.

## Release status

- stable: `1.0.0`
- supports MCP protocol versions: `2024-11-05`, `2025-03-26`, `2025-06-18`
- includes legacy protocol compatibility mode for older clients

## Since initial commit

- native standalone MCP core in C++ with a C ABI (`include/factmcp.h`)
- shared and static library outputs for downstream embedding
- stdio and HTTP transports with JSON-RPC 2.0 request processing
- HTTP endpoints for MCP traffic and discovery (`/mcp`, `/tools`, `/sse`)
- streamable HTTP behavior with GET/POST support and SSE event delivery
- negotiated protocol version handling and response header updates
- legacy SSE handling improvements for older protocol/client behavior
- example stdio servers: echo, math, filesystem/search suite, code/project/knowledge-graph suite
- C++ smoke client utility for quick protocol validation
- protocol and endpoint test coverage for stdio and HTTP flows
- CMake package exports for easy `find_package(faCtMCP)` integration
- validated interoperability against multiple MCP clients (including legacy-leaning client behavior)

## What ships

- shared library build (`faCtMCP.dll`)
- static library build (`faCtMCP_static.lib`)
- public C header: `include/factmcp.h`
- MIT license
- C++ smoke client: `factmcp_smoke_client`
- runnable stdio example servers:
  - `factmcp_stdio_echo`
  - `factmcp_stdio_math`
  - `factmcp_stdio_filesystem_suite`
  - `factmcp_stdio_code_suite`

## Current transport support

- `STDIO`
- `HTTP`

## Public C API

```c
typedef struct factmcp_server factmcp_server;

typedef enum factmcp_transport {
    FACTMCP_TRANSPORT_HTTP = 0,
    FACTMCP_TRANSPORT_STDIO = 1
} factmcp_transport;

typedef const char* (*factmcp_tool_handler)(const char* json_params, void* user_data);

factmcp_server* factmcp_create(factmcp_transport transport, int port);
void factmcp_destroy(factmcp_server* server);
int factmcp_start(factmcp_server* server);
void factmcp_stop(factmcp_server* server);
void factmcp_run(factmcp_server* server);
int factmcp_port(const factmcp_server* server);

int factmcp_register_tool(
    factmcp_server* server,
    const char* name,
    const char* tool_json,
    factmcp_tool_handler handler,
    void* user_data);

const char* factmcp_process_jsonrpc(factmcp_server* server, const char* json_body);
void factmcp_free_string(const char* text);
```

## Minimal embedding flow

1. Create a server with `factmcp_create`
2. Register one or more tools with JSON schema metadata
3. Start the server
4. Run it using stdio or HTTP

## Example

```c
static const char* echo_handler(const char* json_params, void* user_data) {
    (void)user_data;
    return "{\"success\":true,\"message\":\"hello from faCtMCP\"}";
}

int main(void) {
    factmcp_server* server = factmcp_create(FACTMCP_TRANSPORT_STDIO, 0);
    factmcp_register_tool(server, "echo", TOOL_JSON, echo_handler, NULL);
    factmcp_start(server);
    factmcp_run(server);
    factmcp_destroy(server);
    return 0;
}
```

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
ctest -C Release --test-dir build --output-on-failure
```

## Getting Started

Minimal consumer `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_mcp_tool LANGUAGES C CXX)

find_package(faCtMCP REQUIRED)

add_executable(my_mcp_tool main.cpp)
target_link_libraries(my_mcp_tool PRIVATE faCtMCP::faCtMCP)
```

Minimal `main.cpp`:

```cpp
#include "factmcp.h"

static const char* kToolJson =
    "{"
    "\"name\":\"hello\"," 
    "\"description\":\"Return a greeting\"," 
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}"
    "}";

static const char* hello_handler(const char*, void*) {
    return "{\"success\":true,\"message\":\"hello from faCtMCP\"}";
}

int main() {
    factmcp_server* server = factmcp_create(FACTMCP_TRANSPORT_STDIO, 0);
    factmcp_register_tool(server, "hello", kToolJson, hello_handler, nullptr);
    factmcp_start(server);
    factmcp_run(server);
    factmcp_destroy(server);
    return 0;
}
```

## Example binaries

- `factmcp_stdio_echo.exe` exposes an `echo` tool
- `factmcp_stdio_math.exe` exposes an `add` tool
- `factmcp_stdio_filesystem_suite.exe` exposes converted filesystem/search tools:
  - `create_directory`
  - `list_directory`
  - `list_directory_with_sizes`
  - `directory_tree`
  - `move_file`
  - `search_files`
  - `get_file_info`
  - `glob_files`
  - `grep_files`
- `factmcp_stdio_code_suite.exe` exposes converted code/project tools:
  - `symbols`
  - `git`
  - `project`
  - `analyze`
  - `refactor`
  - `create_entities`
  - `create_relations`
  - `add_observations`
  - `delete_entities`
  - `delete_observations`
  - `delete_relations`
  - `read_graph`
  - `search_nodes`
  - `open_nodes`

These are intentionally tiny reference servers showing how consumers can implement their own tool callbacks in a single source file.

## Standalone vs editor-coupled tools

The standalone faCtMCP package currently ships the tools that can run without the live editor UI.

Good standalone examples:
- filesystem/search tools
- git/project/analyze/refactor/symbols tools
- knowledge-graph memory tools

Not shipped as standalone examples yet:
- `read_file` / `write_file` / `edit_file` in their current editor-bridge form
- `sequentialthinking` in its current editor-integrated form
- `bash` in its current `WinuxCmdBridge`-dependent form

Those can be added later either by:
- decoupling them from editor-only bridges, or
- providing standalone faCtMCP-native replacements

## Release layout

- `include/` public headers
- `src/` library source
- `examples/` standalone example servers
- `tests/` protocol and endpoint validation
- `tools/` smoke-test utility sources
- `.github/workflows/` CI definitions
