cmake_minimum_required(VERSION 3.20)
project(svlsp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for editor tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Dependencies (populated in later phases)
# ---------------------------------------------------------------------------
# Phase 1: Emacs test harness — no C++ dependencies.
# Phase 2: lsp-framework, evaluated and added here.
# Phase 4: ANTLR4 C++ runtime, added here via FetchContent.
# Phase 5: SQLite3 amalgamation, added here.
# Unit-test framework (Catch2) chosen and added in Phase 2.

# ---------------------------------------------------------------------------
# Server binary
# ---------------------------------------------------------------------------
add_executable(svlsp
    src/main.cpp
)

target_include_directories(svlsp PRIVATE src)

target_compile_options(svlsp PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)

target_link_options(svlsp PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# ---------------------------------------------------------------------------
# Unit-test binary (stub — test sources added in Phase 2)
# ---------------------------------------------------------------------------
add_executable(unit_tests
    tests/unit/main_test.cpp
)

target_include_directories(unit_tests PRIVATE src)

# ---------------------------------------------------------------------------
# CTest integration
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME unit COMMAND unit_tests)
