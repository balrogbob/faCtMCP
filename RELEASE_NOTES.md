# faCtMCP 1.0.0

Stable release.

## Summary

This 1.0.0 release captures all work delivered since the initial commit, including transport hardening, protocol negotiation, legacy compatibility behavior, and real-world client interoperability improvements.

## New features and changes since initial commit

- Added standalone `faCtMCP` shared and static libraries for native embedding.
- Added stable public C API via `include/factmcp.h`.
- Added MCP transports for stdio and HTTP.
- Added JSON-RPC 2.0 processing in the server core.
- Added HTTP MCP endpoint coverage including `/mcp`, `/tools`, and `/sse` flows.
- Added streamable HTTP behavior with GET and POST handling.
- Added Server-Sent Events (SSE) support for streaming responses.
- Added negotiated MCP protocol version handling.
- Added explicit support for protocol versions `2024-11-05`, `2025-03-26`, and `2025-06-18`.
- Added legacy protocol compatibility mode for older client behavior.
- Improved SSE session management and event broadcast behavior.
- Improved SSE response behavior for mixed modern and legacy client paths.
- Improved POST message handling across protocol version variants.
- Added standalone example stdio servers for practical tool implementations.
- Added bundled example tool suites: echo, math, filesystem/search, and code/project/knowledge-graph.
- Added C++ smoke client utility for integration and protocol checks.
- Added protocol and endpoint tests across stdio and HTTP flows.
- Added installable CMake package export files for downstream `find_package(faCtMCP)` use.
- Verified interoperability with multiple MCP clients, including clients using older protocol expectations.

## Compatibility matrix (1.0.0)

- transports: stdio, HTTP
- HTTP modes: JSON-RPC POST, streamable GET, SSE
- protocol versions: `2024-11-05`, `2025-03-26`, `2025-06-18`
- compatibility: includes legacy protocol handling paths
