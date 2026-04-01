cmake_minimum_required(VERSION 3.16)
project(faCtMCP VERSION 0.1.0 LANGUAGES C CXX)

enable_testing()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

option(FACTMCP_BUILD_SHARED "Build shared library" ON)
option(FACTMCP_BUILD_STATIC "Build static library" ON)
option(FACTMCP_BUILD_EXAMPLES "Build stdio examples" ON)
option(FACTMCP_BUILD_TESTS "Build tests" ON)

set(FACTMCP_CORE_SOURCES
    src/mcp/FastMCPServer.cpp
    src/mcp/ToolDefinitions.cpp
    src/mcp/ToolState.cpp
    src/mcp/InjectionQueue.cpp
    src/capi/factmcp_capi.cpp
)

set(FACTMCP_EXAMPLE_COMMON_SOURCES
    examples/example_server_utils.cpp
)

set(FACTMCP_EXAMPLE_FILESYSTEM_SOURCES
    src/mcp/JsonUtils.cpp
    src/mcp/handlers/FilesystemHandlers.cpp
    src/mcp/handlers/GlobHandler.cpp
    src/mcp/handlers/GrepHandler.cpp
)

set(FACTMCP_EXAMPLE_CODE_SOURCES
    src/mcp/JsonUtils.cpp
    src/editor_core/FileIO.cpp
    src/mcp/handlers/GrepHandler.cpp
    src/mcp/handlers/GitHandler.cpp
    src/mcp/handlers/ProjectHandler.cpp
    src/mcp/handlers/AnalyzeHandler.cpp
    src/mcp/handlers/RefactorHandler.cpp
    src/mcp/handlers/SymbolsHandler.cpp
    src/mcp/handlers/MemoryHandler.cpp
)

set(FACTMCP_PUBLIC_INCLUDE_DIRS
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(FACTMCP_BUILD_SHARED)
    add_library(faCtMCP SHARED ${FACTMCP_CORE_SOURCES})
    target_include_directories(faCtMCP PUBLIC ${FACTMCP_PUBLIC_INCLUDE_DIRS})
    target_compile_definitions(faCtMCP PRIVATE FACTMCP_EXPORTS)
    target_compile_definitions(faCtMCP PUBLIC FACTMCP_SHARED)
    set_target_properties(faCtMCP PROPERTIES EXPORT_NAME faCtMCP OUTPUT_NAME faCtMCP)
endif()

if(FACTMCP_BUILD_STATIC)
    add_library(faCtMCP_static STATIC ${FACTMCP_CORE_SOURCES})
    target_include_directories(faCtMCP_static PUBLIC ${FACTMCP_PUBLIC_INCLUDE_DIRS})
    set_target_properties(faCtMCP_static PROPERTIES EXPORT_NAME faCtMCPStatic OUTPUT_NAME faCtMCP_static)
endif()

if(FACTMCP_BUILD_EXAMPLES)
    if(FACTMCP_BUILD_SHARED)
        set(FACTMCP_EXAMPLE_LIB faCtMCP)
    else()
        set(FACTMCP_EXAMPLE_LIB faCtMCP_static)
    endif()

    add_executable(factmcp_stdio_echo examples/stdio_echo.cpp)
    target_include_directories(factmcp_stdio_echo PRIVATE ${FACTMCP_PUBLIC_INCLUDE_DIRS})
    target_link_libraries(factmcp_stdio_echo PRIVATE ${FACTMCP_EXAMPLE_LIB})

    add_executable(factmcp_stdio_math examples/stdio_math.cpp)
    target_include_directories(factmcp_stdio_math PRIVATE ${FACTMCP_PUBLIC_INCLUDE_DIRS})
    target_link_libraries(factmcp_stdio_math PRIVATE ${FACTMCP_EXAMPLE_LIB})

    add_executable(factmcp_stdio_filesystem_suite
        examples/stdio_filesystem_suite.cpp
        ${FACTMCP_EXAMPLE_COMMON_SOURCES}
        ${FACTMCP_EXAMPLE_FILESYSTEM_SOURCES})
    target_include_directories(factmcp_stdio_filesystem_suite PRIVATE ${FACTMCP_PUBLIC_INCLUDE_DIRS})
    target_link_libraries(factmcp_stdio_filesystem_suite PRIVATE ${FACTMCP_EXAMPLE_LIB})

    add_executable(factmcp_stdio_code_suite
        examples/stdio_code_suite.cpp
        ${FACTMCP_EXAMPLE_COMMON_SOURCES}
        ${FACTMCP_EXAMPLE_CODE_SOURCES})
    target_include_directories(factmcp_stdio_code_suite PRIVATE ${FACTMCP_PUBLIC_INCLUDE_DIRS})
    target_link_libraries(factmcp_stdio_code_suite PRIVATE ${FACTMCP_EXAMPLE_LIB})
endif()

add_executable(factmcp_smoke_client tools/smoke_client.cpp)
target_include_directories(factmcp_smoke_client PRIVATE ${FACTMCP_PUBLIC_INCLUDE_DIRS})

set(FACTMCP_INSTALL_TARGETS)
if(TARGET faCtMCP)
    list(APPEND FACTMCP_INSTALL_TARGETS faCtMCP)
endif()
if(TARGET faCtMCP_static)
    list(APPEND FACTMCP_INSTALL_TARGETS faCtMCP_static)
endif()

if(FACTMCP_INSTALL_TARGETS)
    install(TARGETS ${FACTMCP_INSTALL_TARGETS}
        EXPORT faCtMCPTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

if(FACTMCP_BUILD_EXAMPLES)
    install(TARGETS
        factmcp_stdio_echo
        factmcp_stdio_math
        factmcp_stdio_filesystem_suite
        factmcp_stdio_code_suite
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples)
endif()

install(FILES include/factmcp.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES LICENSE RELEASE_NOTES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(TARGETS factmcp_smoke_client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(EXPORT faCtMCPTargets
    FILE faCtMCPTargets.cmake
    NAMESPACE faCtMCP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/faCtMCP)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/faCtMCPConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/faCtMCPConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/faCtMCP)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/faCtMCPConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/faCtMCPConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/faCtMCPConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/faCtMCP)

if(FACTMCP_BUILD_TESTS)
    add_executable(factmcp_test_stdio tests/test_stdio_client.cpp)
    target_include_directories(factmcp_test_stdio PRIVATE ${FACTMCP_PUBLIC_INCLUDE_DIRS})

    add_executable(factmcp_test_http tests/test_http_server.cpp src/mcp/FastMCPServer.cpp src/mcp/ToolState.cpp src/mcp/InjectionQueue.cpp src/mcp/ToolDefinitions.cpp)
    target_include_directories(factmcp_test_http PRIVATE ${FACTMCP_PUBLIC_INCLUDE_DIRS})

    if(FACTMCP_BUILD_EXAMPLES)
        add_test(NAME factmcp_stdio_echo_test COMMAND factmcp_test_stdio $<TARGET_FILE:factmcp_stdio_echo>)
        add_test(NAME factmcp_stdio_math_test COMMAND factmcp_test_stdio $<TARGET_FILE:factmcp_stdio_math>)
        add_test(NAME factmcp_stdio_filesystem_suite_test COMMAND factmcp_test_stdio $<TARGET_FILE:factmcp_stdio_filesystem_suite>)
        add_test(NAME factmcp_stdio_code_suite_test COMMAND factmcp_test_stdio $<TARGET_FILE:factmcp_stdio_code_suite>)
    endif()

    add_test(NAME factmcp_http_endpoints_test COMMAND factmcp_test_http)
endif()
