#!/usr/bin/env bash
# build.sh — build svlsp from scratch: wipes the target preset's build
# directory (no incremental CMake cache reuse) and reruns configure + build.
# Wraps the cmake --preset workflow documented in handoff.md's "Build and
# test" section.
#
# Usage: tools/build.sh [debug|release] [--target NAME]
#   debug|release   CMake preset to use (default: debug -- matches this
#                   project's Makefile `build` target and most of
#                   handoff.md's documented workflow).
#   --target NAME   Build only this CMake target (default: everything
#                   registered under the preset, including svlsp and
#                   unit_tests).
#
# Examples:
#   tools/build.sh                          # from-scratch debug build, everything
#   tools/build.sh release                  # from-scratch release build, everything
#   tools/build.sh debug --target svlsp     # from-scratch debug build, svlsp only

set -euo pipefail

PRESET="debug"
TARGET=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        debug|release)
            PRESET="$1"
            shift
            ;;
        --target)
            TARGET="${2:?--target requires a name}"
            shift 2
            ;;
        -h|--help)
            sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unrecognized argument '$1' (see --help)" >&2
            exit 1
            ;;
    esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
cd "$ROOT_DIR"

if ! command -v g++-13 >/dev/null 2>&1; then
    echo "error: g++-13 not found on PATH -- required (system g++ doesn't" >&2
    echo "       support C++20; see handoff.md's 'Build and test' section)" >&2
    exit 1
fi

if [ -d "$BUILD_DIR" ]; then
    echo "==> Removing existing build directory: ${BUILD_DIR}"
    rm -rf "$BUILD_DIR"
fi

echo "==> Configuring (${PRESET} preset)"
cmake --preset "$PRESET"

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
