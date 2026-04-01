# faCtMCP 0.1.0 is out

I got tired of fighting interpreter overhead, runtime friction, and the general feeling that every time you want to build serious agent tooling, you are one dependency spiral away from pain.

At some point the reaction stops being *"surely there is a clean way to do this"* and becomes:

**fuck it, we'll do it live.**

So I did.

`faCtMCP` is a standalone MCP server core with a C ABI, written as a native implementation for people who want fast, embeddable MCP infrastructure without dragging an entire scripting ecosystem behind it.

This release comes out of weeks of converting, reworking, and hardening MCP tooling concepts that were originally living across TypeScript, JavaScript, and Python - and rebuilding them into native C++ with a C-facing library surface.

## What it is

- shared and static library builds
- public C API via `factmcp.h`
- stdio and HTTP MCP transports
- installable CMake package exports
- native C++ smoke client
- protocol and endpoint tests
- standalone example MCP servers

## What it ships with

- `factmcp_stdio_echo`
- `factmcp_stdio_math`
- `factmcp_stdio_filesystem_suite`
- `factmcp_stdio_code_suite`

And these are not decorative examples. They include real converted tool bundles for things like:

- filesystem and search operations
- glob and grep tools
- git and project inspection
- code analysis and refactor previews
- symbol/navigation helpers
- knowledge-graph style memory tools

## Why I made it

Because I wanted:

- a native MCP core
- a reusable library instead of another app-specific tangle
- something embeddable from C/C++ and callable from anything that can speak C ABI
- something that does not make me feel like I am negotiating with a runtime every time I want performance or control

## First release highlights

- MIT licensed
- packaged for downstream use
- tested stdio framing
- tested HTTP routes like `/mcp`, `/tools`, and `/sse`
- ready to live in its own public repo

## Who this is for

If you are building:

- native agent tools
- local MCP servers
- embedded tool runtimes
- host applications that need to expose serious MCP capabilities

this is for you.

## In plain English

I got annoyed enough to rewrite protocol infrastructure in a totally different language so it would behave the way I wanted.

Now other people can use it too.
