# Grammar profiling harness (plan.md §6.33 C2) -- a standalone project, not
# part of the svlsp build. Driven by tools/grammar_profile.sh; see the usage
# there.
#
#   SV_GRAMMAR       the grammar to profile (any file name; copied to Sv.g4)
#   SVLSP_BUILD_DIR  an svlsp build tree whose ANTLR runtime is reused
#                    (default <repo>/build/release)
cmake_minimum_required(VERSION 3.20)
project(svlsp_grammar_profile CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(SVLSP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SV_GRAMMAR "${SVLSP_ROOT}/grammar/Sv.g4" CACHE FILEPATH "Grammar to profile")
set(SVLSP_BUILD_DIR "${SVLSP_ROOT}/build/release" CACHE PATH "svlsp build tree")

set(ANTLR_RUNTIME_INCLUDE
    "${SVLSP_BUILD_DIR}/_deps/antlr4_runtime-src/runtime/Cpp/runtime/src")
set(ANTLR_RUNTIME_LIB
    "${SVLSP_BUILD_DIR}/_deps/antlr4_runtime-build/runtime/libantlr4-runtime.a")
foreach(p "${ANTLR_RUNTIME_INCLUDE}" "${ANTLR_RUNTIME_LIB}")
    if(NOT EXISTS "${p}")
        message(FATAL_ERROR "${p} not found -- build svlsp in ${SVLSP_BUILD_DIR} first")
    endif()
endforeach()

list(APPEND CMAKE_MODULE_PATH "${SVLSP_ROOT}/cmake")
include(ANTLR4Tool)

# The generated classes are named after the grammar (`grammar Sv;`), and ANTLR
# requires the file name to match, so the copy is always Sv.g4. configure_file
# makes a grammar edit re-run the generation.
set(GRAMMAR_COPY "${CMAKE_BINARY_DIR}/grammar/Sv.g4")
configure_file("${SV_GRAMMAR}" "${GRAMMAR_COPY}" COPYONLY)

set(GEN "${CMAKE_BINARY_DIR}/generated")
set(GEN_CPP "${GEN}/SvLexer.cpp" "${GEN}/SvParser.cpp" "${GEN}/SvListener.cpp"
    "${GEN}/SvBaseListener.cpp")
add_custom_command(
    OUTPUT ${GEN_CPP} "${GEN}/SvLexer.h" "${GEN}/SvParser.h" "${GEN}/SvBaseListener.h"
    COMMAND ${ANTLR4_TOOL_COMMAND} -Dlanguage=Cpp -no-visitor -o "${GEN}"
            "${GRAMMAR_COPY}"
    DEPENDS "${GRAMMAR_COPY}"
    COMMENT "Generating the parser from ${SV_GRAMMAR}"
    VERBATIM)

add_library(sv_parser STATIC ${GEN_CPP})
target_include_directories(sv_parser PUBLIC "${GEN}" "${ANTLR_RUNTIME_INCLUDE}")
target_compile_definitions(sv_parser PUBLIC ANTLR4CPP_STATIC)
target_compile_options(sv_parser PRIVATE -w)
target_link_libraries(sv_parser PUBLIC "${ANTLR_RUNTIME_LIB}" pthread)

add_executable(grammar_profile profile_main.cpp)
target_link_libraries(grammar_profile PRIVATE sv_parser)

# svlsp's own walker against this grammar's parser: a grammar change that
# needs a walker change is checked with the walker as edited in the tree.
add_executable(walk_dump walk_dump.cpp
    "${SVLSP_ROOT}/src/compiler/sv_tree_walker.cpp"
    "${SVLSP_ROOT}/src/compiler/doc_comment.cpp")
target_include_directories(walk_dump PRIVATE "${SVLSP_ROOT}/src")
target_link_libraries(walk_dump PRIVATE sv_parser)

# The preprocessor does not depend on the grammar: it is built from svlsp's
# own sources.
add_executable(pp_dump pp_dump.cpp
    "${SVLSP_ROOT}/src/compiler/sv_preprocessor.cpp"
    "${SVLSP_ROOT}/src/compiler/compiler_directive_stripper.cpp"
    "${SVLSP_ROOT}/src/compiler/doc_comment.cpp")
target_include_directories(pp_dump PRIVATE "${SVLSP_ROOT}/src")
