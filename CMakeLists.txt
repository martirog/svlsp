# 3.24: CMakePresets.json format version 3 (3.21) and
# DOWNLOAD_EXTRACT_TIMESTAMP (3.24). tools/build.sh checks the same minimum.
cmake_minimum_required(VERSION 3.24)
project(svlsp VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Prerequisites: a compiler with the C++20 features the code uses (the probe
# tools/find-cxx20.sh selects a compiler with), and the cloned submodules.
# ---------------------------------------------------------------------------
set(_svlsp_cxx20_hint
    "Use GCC >= 10 or Clang >= 14 with a C++20 standard library -- "
    "tools/build.sh picks one, or pass -DCMAKE_CXX_COMPILER=... "
    "-DCMAKE_C_COMPILER=... (in a fresh build directory).")
if(NOT "cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(FATAL_ERROR
        "${CMAKE_CXX_COMPILER} (${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION}) has no C++20 mode.\n"
        ${_svlsp_cxx20_hint})
endif()
try_compile(SVLSP_CXX20_PROBE_OK
    "${CMAKE_BINARY_DIR}/cxx20_probe"
    SOURCES "${CMAKE_SOURCE_DIR}/cmake/cxx20_probe.cpp"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    OUTPUT_VARIABLE _svlsp_cxx20_probe_log
)
if(NOT SVLSP_CXX20_PROBE_OK)
    message(FATAL_ERROR
        "${CMAKE_CXX_COMPILER} (${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION}) can't build C++20 code: "
        "cmake/cxx20_probe.cpp failed to compile.\n"
        ${_svlsp_cxx20_hint} "\n"
        "${_svlsp_cxx20_probe_log}")
endif()

foreach(_svlsp_submodule third_party/lsp-framework)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${_svlsp_submodule}/CMakeLists.txt")
        message(FATAL_ERROR
            "Submodule ${_svlsp_submodule} is not cloned. Run:\n"
            "  git submodule update --init --recursive")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Version info for `svlsp --version` -- resolved at build time: target
# svlsp_git_version runs cmake/GitVersion.cmake on every build and rewrites
# svlsp_git_version.h only when `git describe` changed (a commit, or the
# tree turning clean/dirty), so the version never lags the sources it was
# built from. "unknown" outside a git checkout (e.g. a source tarball); no
# repo tags exist, so this is always just the abbreviated commit hash,
# with a "-dirty" suffix if tracked files have uncommitted changes.
# ---------------------------------------------------------------------------
find_package(Git QUIET)
set(SVLSP_GIT_VERSION_DIR "${CMAKE_BINARY_DIR}/version")
add_custom_target(svlsp_git_version
    COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT=${SVLSP_GIT_VERSION_DIR}/svlsp_git_version.h
        -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
    BYPRODUCTS ${SVLSP_GIT_VERSION_DIR}/svlsp_git_version.h
    COMMENT "Checking git version"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# A vendored target's include directories become system include directories
# for its consumers, so warnings in its headers (e.g. ANTLR's hidden
# ParseTree::operator==) don't land in svlsp's translation units.
# (CMake >= 3.25 could say SYSTEM on add_subdirectory / FetchContent_Declare.)
function(svlsp_system_includes target)
    get_target_property(_dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
    set_property(TARGET ${target} APPEND PROPERTY
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${_dirs})
endfunction()

# lsp-framework (Phase 2): LSP protocol layer — zero external dependencies,
# LSP 3.17, header+generated types built as a static library.
set(LSP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LSP_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/lsp-framework)
# Vendored code (plan.md §6.24): built without warnings, like the generated
# parser and SQLite, and its headers are system headers to svlsp's targets.
target_compile_options(lsp PRIVATE -w)
svlsp_system_includes(lsp)

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
set(ANTLR_BUILD_CPP_TESTS  OFF CACHE BOOL "" FORCE) # else it fetches and builds googletest
set(ANTLR4_INSTALL         OFF CACHE BOOL "" FORCE)
set(WITH_DEMO              FALSE CACHE BOOL "" FORCE)
set(ANTLR_BUILD_SHARED     OFF CACHE BOOL "" FORCE) # only antlr4_static is linked
set(DISABLE_WARNINGS       ON  CACHE BOOL "" FORCE) # vendored, as lsp above
FetchContent_MakeAvailable(antlr4_runtime)
svlsp_system_includes(antlr4_static)

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
target_include_directories(svlsp_antlr4 SYSTEM PUBLIC "${ANTLR4_GENERATED_DIR}")
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
    # No CMakeLists.txt inside, so MakeAvailable only downloads it (the
    # one-argument FetchContent_Populate is deprecated since CMake 3.30).
    FetchContent_MakeAvailable(sqlite3_amalgamation)
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
    # The build is warning-free (plan.md §6.24): keep it so where the tests
    # run, without failing a release build on a compiler that warns more.
    $<$<CONFIG:Debug>:-Werror>
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
    src/compiler/doc_comment.cpp
    src/compiler/filelist_parser.cpp
    src/compiler/file_utils.cpp
    src/compiler/content_hash.cpp
    src/compiler/parse_worker.cpp
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
    src/db/project_refresher.cpp
)
target_include_directories(svlsp_db PUBLIC src)
target_link_libraries(svlsp_db PUBLIC svlsp_sqlite3 svlsp_compiler svlsp_options)

# ---------------------------------------------------------------------------
# Server library (business logic, shared by svlsp binary and unit_tests)
# ---------------------------------------------------------------------------
add_library(svlsp_lib STATIC
    src/lsp/change_debouncer.cpp
    src/lsp/completion.cpp
    src/lsp/definition.cpp
    src/lsp/document_symbols.cpp
    src/lsp/diagnostics.cpp
    src/lsp/document_store.cpp
    src/lsp/fuzzy_match.cpp
    src/lsp/hover.cpp
    src/lsp/macro_resolution.cpp
    src/lsp/references.cpp
    src/lsp/rename.cpp
    src/lsp/signature_help.cpp
    src/lsp/server_state.cpp
    src/lsp/symbol_utils.cpp
    src/lsp/symbol_resolution.cpp
    src/lsp/workspace_symbols.cpp
    src/lsp/project_manifest_parser.cpp
    src/lsp/project_registry.cpp
    src/lsp/library_db_builder.cpp
    src/lsp/server.cpp
)
target_include_directories(svlsp_lib PUBLIC src)
target_link_libraries(svlsp_lib PUBLIC lsp svlsp_compiler svlsp_db svlsp_options)

# ---------------------------------------------------------------------------
# Server binary
# ---------------------------------------------------------------------------
add_executable(svlsp src/main.cpp)
target_link_libraries(svlsp PRIVATE svlsp_lib)
add_dependencies(svlsp svlsp_git_version)
target_include_directories(svlsp PRIVATE ${SVLSP_GIT_VERSION_DIR})
target_compile_definitions(svlsp PRIVATE
    SVLSP_VERSION="${PROJECT_VERSION}"
    SVLSP_BUILD_TYPE="$<CONFIG>"
)

# ---------------------------------------------------------------------------
# Unit-test binary
# ---------------------------------------------------------------------------
add_executable(unit_tests
    tests/unit/main_test.cpp
    tests/unit/lsp/test_server_state.cpp
    tests/unit/lsp/test_document_store.cpp
    tests/unit/lsp/test_change_debouncer.cpp
    tests/unit/lsp/test_server_debounce.cpp
    tests/unit/lsp/test_server_diagnostics_visibility.cpp
    tests/unit/lsp/test_server_cross_file_invalidation.cpp
    tests/unit/lsp/test_symbol_utils.cpp
    tests/unit/lsp/test_request_log.cpp
    tests/unit/lsp/test_symbol_resolution.cpp
    tests/unit/lsp/test_completion_visibility.cpp
    tests/unit/lsp/test_fuzzy_match.cpp
    tests/unit/lsp/test_completion.cpp
    tests/unit/lsp/test_doc_comment_providers.cpp
    tests/unit/lsp/test_completion_library_attach.cpp
    tests/unit/lsp/test_completion_latency.cpp
    tests/unit/lsp/test_sv_keywords.cpp
    tests/unit/lsp/test_sv_builtin_methods.cpp
    tests/unit/lsp/test_definition.cpp
    tests/unit/lsp/test_definition_scoped.cpp
    tests/unit/lsp/test_diagnostics.cpp
    tests/unit/lsp/test_document_symbols.cpp
    tests/unit/lsp/test_hover.cpp
    tests/unit/lsp/test_references.cpp
    tests/unit/lsp/test_references_scoped.cpp
    tests/unit/lsp/test_references_library.cpp
    tests/unit/lsp/test_type_ref_check.cpp
    tests/unit/lsp/test_rename.cpp
    tests/unit/lsp/test_signature_help.cpp
    tests/unit/lsp/test_macro_resolution.cpp
    tests/unit/lsp/test_workspace_symbols.cpp
    tests/unit/lsp/test_project_manifest_parser.cpp
    tests/unit/lsp/test_project_registry.cpp
    tests/unit/lsp/test_library_db_builder.cpp
    tests/unit/compiler/test_sv_parser.cpp
    tests/unit/compiler/test_compiler_directive_stripper.cpp
    tests/unit/compiler/test_sv_preprocessor.cpp
    tests/unit/compiler/test_content_hash.cpp
    tests/unit/compiler/test_sv_listener.cpp
    tests/unit/compiler/test_doc_comments.cpp
    tests/unit/compiler/test_sv_error_recovery.cpp
    tests/unit/compiler/test_parse_cache.cpp
    tests/unit/compiler/test_filelist_parser.cpp
    tests/unit/compiler/test_parse_worker.cpp
    tests/unit/db/test_database.cpp
    tests/unit/db/test_symbol_database.cpp
    tests/unit/db/test_symbol_database_library_attach.cpp
    tests/unit/db/test_symbol_docs.cpp
    tests/unit/db/test_compilation_controller.cpp
    tests/unit/db/test_library_resolver.cpp
    tests/unit/db/test_project_compiler.cpp
    tests/unit/db/test_project_refresher.cpp
)
target_compile_definitions(unit_tests PRIVATE
    SV_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    SVLSP_EXE="$<TARGET_FILE:svlsp>"
)
add_dependencies(unit_tests svlsp) # the parse worker tests run it
target_link_libraries(unit_tests PRIVATE svlsp_lib svlsp_antlr4 svlsp_db Catch2::Catch2WithMain)
catch_discover_tests(unit_tests)

# ---------------------------------------------------------------------------
# UVM-corpus test binary
#
# Exercises the DB-backed LSP feature providers (hover, definition,
# documentSymbol, workspace/symbol, completion) against the real, external,
# ~140-file UVM-core corpus compiled through the actual production pipeline.
# Deliberately NOT added to CTest below: the corpus isn't part of this repo
# and the one-time compile takes 12-17 minutes (see handoff.md's "UVM
# real-world smoke test" sessions), so it must stay opt-in, run directly:
#   ./build/release/uvm_corpus_tests
# Point SVLSP_UVM_CORPUS_DIR at a UVM-core src/ checkout if not using the
# default path (see tests/uvm_corpus/uvm_corpus_fixture.cpp).
# ---------------------------------------------------------------------------
add_executable(uvm_corpus_tests
    tests/uvm_corpus/uvm_corpus_fixture.cpp
    tests/uvm_corpus/test_hover_uvm.cpp
    tests/uvm_corpus/test_definition_uvm.cpp
    tests/uvm_corpus/test_document_symbols_uvm.cpp
    tests/uvm_corpus/test_workspace_symbols_uvm.cpp
    tests/uvm_corpus/test_completion_uvm.cpp
    tests/uvm_corpus/test_signature_help_uvm.cpp
    tests/uvm_corpus/test_references_uvm.cpp
)
target_include_directories(uvm_corpus_tests PRIVATE tests/uvm_corpus)
target_link_libraries(uvm_corpus_tests PRIVATE svlsp_lib svlsp_antlr4 svlsp_db Catch2::Catch2WithMain)

# ---------------------------------------------------------------------------
# CTest
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME unit COMMAND unit_tests)
