#!/usr/bin/env bash
# build.sh — build svlsp from scratch: wipes the target preset's build
# directory (no incremental CMake cache reuse) and reruns configure + build.
# Wraps the cmake --preset workflow documented in handoff.md's "Build and
# test" section.
#
# Usage: tools/build.sh [debug|release] [--target NAME] [--output-dir DIR] [--check]
#   debug|release    CMake preset to use (default: debug -- matches this
#                    project's Makefile `build` target and most of
#                    handoff.md's documented workflow).
#   --target NAME    Build only this CMake target (default: everything
#                    registered under the preset, including svlsp and
#                    unit_tests).
#   --output-dir DIR After building, copy the svlsp binary into DIR (created
#                    if missing) -- useful since a preset's own build
#                    directory is fixed (build/debug or build/release,
#                    CMakePresets.json's binaryDir) and can't be
#                    redirected on the command line. Implies --target
#                    svlsp when --target wasn't also given explicitly,
#                    since there's normally no reason to build anything
#                    else just to copy the binary elsewhere.
#   --check          Only check the prerequisites (CMake >= 3.24, git, the
#                    submodules, antlr4 or Java >= 11, a C++20 compiler --
#                    $CXX/$CC if set, see tools/find-cxx20.sh) and exit.
#                    A build runs the same checks before wiping anything.
#
# Examples:
#   tools/build.sh                                   # from-scratch debug build, everything
#   tools/build.sh release                           # from-scratch release build, everything
#   tools/build.sh debug --target svlsp              # from-scratch debug build, svlsp only
#   tools/build.sh release --output-dir ~/bin        # svlsp only, copied to ~/bin/svlsp
#   CXX=clang++-19 tools/build.sh release            # with a chosen compiler

set -euo pipefail

PRESET="debug"
TARGET=""
TARGET_EXPLICIT=""
OUTPUT_DIR=""
CHECK_ONLY=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        debug|release)
            PRESET="$1"
            shift
            ;;
        --target)
            TARGET="${2:?--target requires a name}"
            TARGET_EXPLICIT="1"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="${2:?--output-dir requires a path}"
            shift 2
            ;;
        --check)
            CHECK_ONLY="1"
            shift
            ;;
        -h|--help)
            sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unrecognized argument '$1' (see --help)" >&2
            exit 1
            ;;
    esac
done

if [ -n "$OUTPUT_DIR" ] && [ -z "$TARGET_EXPLICIT" ]; then
    TARGET="svlsp"
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
cd "$ROOT_DIR"

# --- Prerequisites, checked before the build directory is wiped -----------

# CMake 3.24: CMakePresets.json is format version 3 (3.21) and the SQLite
# fallback fetch uses DOWNLOAD_EXTRACT_TIMESTAMP (3.24). Keep in step with
# cmake_minimum_required in CMakeLists.txt.
CMAKE_MIN="3.24"
version_at_least() {
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}
if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake not found on PATH (>= ${CMAKE_MIN} required)" >&2
    exit 1
fi
CMAKE_VERSION="$(cmake --version | sed -n 's/^cmake version \([0-9.]*\).*/\1/p')"
if ! version_at_least "$CMAKE_VERSION" "$CMAKE_MIN"; then
    echo "error: cmake ${CMAKE_VERSION} is too old (>= ${CMAKE_MIN} required)" >&2
    exit 1
fi
echo "==> cmake ${CMAKE_VERSION}"

# git: FetchContent clones Catch2 and the ANTLR4 runtime.
if ! command -v git >/dev/null 2>&1; then
    echo "error: git not found on PATH (configure clones Catch2 and the ANTLR4 runtime)" >&2
    exit 1
fi

# Submodules (third_party/lsp-framework).
if [ -f .gitmodules ]; then
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        missing=""
        while read -r line; do
            path="$(echo "${line:1}" | awk '{print $2}')"
            case "${line:0:1}" in
                -) missing+=" ${path}" ;;
                U) echo "error: submodule ${path} has merge conflicts" >&2; exit 1 ;;
                +) echo "warning: submodule ${path} is not at the commit this tree records" \
                        "(git submodule update --recursive to sync)" >&2 ;;
            esac
        done < <(git submodule status --recursive)
        if [ -n "$missing" ]; then
            echo "error: submodule(s) not cloned:${missing}" >&2
            echo "       run: git submodule update --init --recursive" >&2
            exit 1
        fi
    else
        # Not a git checkout (e.g. an exported tree): just require content.
        while read -r path; do
            if [ -z "$(ls -A "$path" 2>/dev/null)" ]; then
                echo "error: submodule directory ${path} is empty" >&2
                exit 1
            fi
        done < <(git config -f .gitmodules --get-regexp '\.path$' | awk '{print $2}')
    fi
fi

# ANTLR4 code generator: `antlr4` on PATH, else Java >= 11 to run the
# downloaded 4.13.2 JAR (cmake/ANTLR4Tool.cmake checks the tool's version).
if ! command -v antlr4 >/dev/null 2>&1; then
    if ! command -v java >/dev/null 2>&1; then
        echo "error: neither antlr4 nor java found on PATH -- one is needed to" >&2
        echo "       generate the parser (pip install antlr4-tools, or a Java >= 11 runtime)" >&2
        exit 1
    fi
    JAVA_MAJOR="$(java -version 2>&1 | sed -n 's/.*version "\(1\.\)\{0,1\}\([0-9]*\).*/\2/p' | head -n1)"
    if [ -n "$JAVA_MAJOR" ] && [ "$JAVA_MAJOR" -lt 11 ]; then
        echo "error: java ${JAVA_MAJOR} is too old for ANTLR 4.13 (>= 11 required)" >&2
        exit 1
    fi
fi

# A C++20 compiler (honours CXX/CC).
COMPILER_ARGS_TEXT="$(tools/find-cxx20.sh)" || exit 1
mapfile -t COMPILER_ARGS <<< "$COMPILER_ARGS_TEXT"
echo "==> Compilers: ${COMPILER_ARGS[*]}"

if [ -n "$CHECK_ONLY" ]; then
    echo "==> Prerequisites OK."
    exit 0
fi

# --- Build -----------------------------------------------------------------

if [ -d "$BUILD_DIR" ]; then
    echo "==> Removing existing build directory: ${BUILD_DIR}"
    rm -rf "$BUILD_DIR"
fi

echo "==> Configuring (${PRESET} preset)"
cmake --preset "$PRESET" "${COMPILER_ARGS[@]}"

echo "==> Building (${PRESET} preset)${TARGET:+, target: $TARGET}"
if [ -n "$TARGET" ]; then
    cmake --build --preset "$PRESET" --target "$TARGET"
else
    cmake --build --preset "$PRESET"
fi

echo "==> Done."
if [ -x "${BUILD_DIR}/svlsp" ]; then
    echo "    Binary: ${BUILD_DIR}/svlsp"
fi

if [ -n "$OUTPUT_DIR" ]; then
    if [ ! -x "${BUILD_DIR}/svlsp" ]; then
        echo "error: --output-dir given but ${BUILD_DIR}/svlsp wasn't built" >&2
        exit 1
    fi
    mkdir -p "$OUTPUT_DIR"
    cp "${BUILD_DIR}/svlsp" "$OUTPUT_DIR/"
    echo "    Copied to: ${OUTPUT_DIR}/svlsp"
fi
