cmake_minimum_required(VERSION 3.20)
project(svlsp VERSION 0.1.0 LANGUAGES CXX C)

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

# Phase 4: ANTLR4 C++ runtime — fetched from the official source zip (C++ only,
# much smaller than cloning the whole antlr4 repo).
FetchContent_Declare(
    antlr4_runtime
    GIT_REPOSITORY https://github.com/antlr/antlr4.git
    GIT_TAG        4.13.2
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  runtime/Cpp
)
set(ANTLR4_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(ANTLR4_INSTALL         OFF CACHE BOOL "" FORCE)
set(WITH_DEMO              FALSE CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(antlr4_runtime)

# Grammar code generation: Sv.g4 → C++ lexer/parser/visitor sources.
set(SV_GRAMMAR "${CMAKE_SOURCE_DIR}/grammar/Sv.g4")
set(ANTLR4_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/antlr4")
file(MAKE_DIRECTORY "${ANTLR4_GENERATED_DIR}")

set(ANTLR4_GENERATED_CPP
    "${ANTLR4_GENERATED_DIR}/SvLexer.cpp"
    "${ANTLR4_GENERATED_DIR}/SvParser.cpp"
    "${ANTLR4_GENERATED_DIR}/SvListener.cpp"
    "${ANTLR4_GENERATED_DIR}/SvVisitor.cpp"
    "${ANTLR4_GENERATED_DIR}/SvBaseListener.cpp"
    "${ANTLR4_GENERATED_DIR}/SvBaseVisitor.cpp"
)

add_custom_command(
    OUTPUT
        ${ANTLR4_GENERATED_CPP}
        "${ANTLR4_GENERATED_DIR}/SvLexer.h"
        "${ANTLR4_GENERATED_DIR}/SvParser.h"
        "${ANTLR4_GENERATED_DIR}/SvListener.h"
        "${ANTLR4_GENERATED_DIR}/SvVisitor.h"
        "${ANTLR4_GENERATED_DIR}/SvBaseListener.h"
        "${ANTLR4_GENERATED_DIR}/SvBaseVisitor.h"
    COMMAND ${ANTLR4_TOOL_COMMAND}
            -Dlanguage=Cpp
            -visitor
            -o "${ANTLR4_GENERATED_DIR}"
            "${SV_GRAMMAR}"
    DEPENDS "${SV_GRAMMAR}"
    COMMENT "Generating ANTLR4 C++ parser/lexer from Sv.g4"
    VERBATIM
)

# Generated parser target — strict warnings suppressed (machine-generated code).
add_library(svlsp_antlr4 STATIC ${ANTLR4_GENERATED_CPP})
target_include_directories(svlsp_antlr4 PUBLIC "${ANTLR4_GENERATED_DIR}")
target_link_libraries(svlsp_antlr4 PUBLIC antlr4_static)
target_compile_options(svlsp_antlr4 PRIVATE -w)

# Phase 5: SQLite3 — use system library if available, otherwise fetch the
# official amalgamation (single sqlite3.c + sqlite3.h) at configure time.
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
    add_library(svlsp_sqlite3 INTERFACE)
    target_link_libraries(svlsp_sqlite3 INTERFACE SQLite::SQLite3)
else()
    message(STATUS "SQLite3 not found — fetching amalgamation 3.47.0")
    FetchContent_Declare(
        sqlite3_amalgamation
        URL      https://www.sqlite.org/2024/sqlite-amalgamation-3470000.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_Populate(sqlite3_amalgamation)
    add_library(svlsp_sqlite3 STATIC
        "${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c")
    target_include_directories(svlsp_sqlite3 PUBLIC
        "${sqlite3_amalgamation_SOURCE_DIR}")
    target_compile_options(svlsp_sqlite3 PRIVATE -w)
    set_property(TARGET svlsp_sqlite3 PROPERTY C_STANDARD 99)
    target_link_libraries(svlsp_sqlite3 PUBLIC ${CMAKE_DL_LIBS})
endif()

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
# Compiler library — no LSP or DB dependencies; used by both svlsp_db and
# svlsp_lib to avoid a circular dependency.
# ---------------------------------------------------------------------------
add_library(svlsp_compiler STATIC
    src/compiler/compiler_directive_stripper.cpp
    src/compiler/parse_cache.cpp
    src/compiler/sv_preprocessor.cpp
    src/compiler/sv_tree_walker.cpp
    src/compiler/filelist_parser.cpp
    src/compiler/file_utils.cpp
)
target_include_directories(svlsp_compiler PUBLIC src)
target_link_libraries(svlsp_compiler PUBLIC svlsp_antlr4 svlsp_options)

# ---------------------------------------------------------------------------
# Database library (Phase 5)
# ---------------------------------------------------------------------------
add_library(svlsp_db STATIC
    src/db/database.cpp
    src/db/symbol_database.cpp
    src/db/compilation_controller.cpp
    src/db/library_resolver.cpp
    src/db/project_compiler.cpp
)
target_include_directories(svlsp_db PUBLIC src)
target_link_libraries(svlsp_db PUBLIC svlsp_sqlite3 svlsp_compiler svlsp_options)

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
    src/lsp/symbol_utils.cpp
    src/lsp/workspace_symbols.cpp
    src/lsp/project_manifest_parser.cpp
    src/lsp/project_registry.cpp
    src/lsp/server.cpp
)
target_include_directories(svlsp_lib PUBLIC src)
target_link_libraries(svlsp_lib PUBLIC lsp svlsp_compiler svlsp_db svlsp_options)

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
    tests/unit/lsp/test_symbol_utils.cpp
    tests/unit/lsp/test_completion.cpp
    tests/unit/lsp/test_definition.cpp
    tests/unit/lsp/test_diagnostics.cpp
    tests/unit/lsp/test_document_symbols.cpp
    tests/unit/lsp/test_hover.cpp
    tests/unit/lsp/test_references.cpp
    tests/unit/lsp/test_rename.cpp
    tests/unit/lsp/test_signature_help.cpp
    tests/unit/lsp/test_workspace_symbols.cpp
    tests/unit/lsp/test_project_manifest_parser.cpp
    tests/unit/lsp/test_project_registry.cpp
    tests/unit/compiler/test_sv_parser.cpp
    tests/unit/compiler/test_compiler_directive_stripper.cpp
    tests/unit/compiler/test_sv_preprocessor.cpp
    tests/unit/compiler/test_sv_listener.cpp
    tests/unit/compiler/test_sv_error_recovery.cpp
    tests/unit/compiler/test_parse_cache.cpp
    tests/unit/compiler/test_filelist_parser.cpp
    tests/unit/db/test_database.cpp
    tests/unit/db/test_symbol_database.cpp
    tests/unit/db/test_compilation_controller.cpp
    tests/unit/db/test_library_resolver.cpp
    tests/unit/db/test_project_compiler.cpp
)
target_compile_definitions(unit_tests PRIVATE
    SV_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
)
target_link_libraries(unit_tests PRIVATE svlsp_lib svlsp_antlr4 svlsp_db Catch2::Catch2WithMain)
catch_discover_tests(unit_tests)

# ---------------------------------------------------------------------------
# CTest
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME unit COMMAND unit_tests)
