cmake_minimum_required(VERSION 3.20)
project(svlsp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# lsp-framework (Phase 2): LSP protocol layer — zero external dependencies,
# LSP 3.17, header+generated types built as a static library.
set(LSP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LSP_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/lsp-framework)

# Catch2 (Phase 2): unit-test framework, fetched at configure time.
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

# Phase 4: ANTLR4 code-generation tool (antlr4 from PATH, or JAR download fallback).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(ANTLR4Tool)

# Phase 4: ANTLR4 C++ runtime — added here.
# Phase 5: SQLite3 amalgamation — added here.

# ---------------------------------------------------------------------------
# Common compile options
# ---------------------------------------------------------------------------
add_library(svlsp_options INTERFACE)
target_compile_options(svlsp_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)
target_link_options(svlsp_options INTERFACE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# ---------------------------------------------------------------------------
# Server library (business logic, shared by svlsp binary and unit_tests)
# ---------------------------------------------------------------------------
add_library(svlsp_lib STATIC
    src/lsp/completion.cpp
    src/lsp/definition.cpp
    src/lsp/document_symbols.cpp
    src/lsp/diagnostics.cpp
    src/lsp/document_store.cpp
    src/lsp/hover.cpp
    src/lsp/references.cpp
    src/lsp/rename.cpp
    src/lsp/signature_help.cpp
    src/lsp/server_state.cpp
    src/lsp/workspace_symbols.cpp
    src/lsp/server.cpp
)
target_include_directories(svlsp_lib PUBLIC src)
target_link_libraries(svlsp_lib PUBLIC lsp svlsp_options)

# ---------------------------------------------------------------------------
# Server binary
# ---------------------------------------------------------------------------
add_executable(svlsp src/main.cpp)
target_link_libraries(svlsp PRIVATE svlsp_lib)

# ---------------------------------------------------------------------------
# Unit-test binary
# ---------------------------------------------------------------------------
add_executable(unit_tests
    tests/unit/main_test.cpp
    tests/unit/lsp/test_server_state.cpp
    tests/unit/lsp/test_document_store.cpp
    tests/unit/lsp/test_completion.cpp
    tests/unit/lsp/test_definition.cpp
    tests/unit/lsp/test_diagnostics.cpp
    tests/unit/lsp/test_document_symbols.cpp
    tests/unit/lsp/test_hover.cpp
    tests/unit/lsp/test_references.cpp
    tests/unit/lsp/test_rename.cpp
    tests/unit/lsp/test_signature_help.cpp
    tests/unit/lsp/test_workspace_symbols.cpp
)
target_link_libraries(unit_tests PRIVATE svlsp_lib Catch2::Catch2WithMain)
catch_discover_tests(unit_tests)

# ---------------------------------------------------------------------------
# CTest
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME unit COMMAND unit_tests)
