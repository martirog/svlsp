#!/usr/bin/env bash
# find-cxx20.sh — pick a C++ compiler that builds svlsp (C++20, checked by
# compiling and linking cmake/cxx20_probe.cpp) and its matching C compiler.
#
# Usage: tools/find-cxx20.sh
#   Prints the CMake arguments for the chosen pair on stdout, one per line:
#     -DCMAKE_CXX_COMPILER=/usr/bin/g++-13
#     -DCMAKE_C_COMPILER=/usr/bin/gcc-13
#   and exits 1 (with the candidates tried on stderr) if none works.
#
# $CXX (and $CC), when set, is the only candidate tried. Otherwise: g++,
# then g++-NN newest first, then clang++, then clang++-NN newest first --
# the first that passes the probe wins.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="${ROOT_DIR}/cmake/cxx20_probe.cpp"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

# Versioned names on PATH (g++-13, clang++-19, ...), newest first.
versioned() {
    local name
    compgen -c "$1-" | sort -u | while read -r name; do
        [[ "${name#"$1"-}" =~ ^[0-9]+(\.[0-9]+)*$ ]] && echo "${name#"$1"-}"
    done | sort -rV | sed "s/^/$1-/"
}

# The C compiler that goes with a C++ one: g++-13 -> gcc-13,
# clang++-19 -> clang-19, c++ -> cc.
c_compiler_for() {
    local dir base
    dir="$(dirname "$1")"
    base="$(basename "$1")"
    case "$base" in
        g++*)     base="gcc${base#g++}" ;;
        clang++*) base="clang${base#clang++}" ;;
        c++*)     base="cc${base#c++}" ;;
        *)        base="cc" ;;
    esac
    if [ -x "${dir}/${base}" ]; then
        echo "${dir}/${base}"
    else
        command -v "$base" || command -v cc || true
    fi
}

probe() {
    "$1" -std=c++20 "$PROBE" -o "${TMP_DIR}/probe" >"${TMP_DIR}/probe.log" 2>&1
}

if [ -n "${CXX:-}" ]; then
    candidates=("$CXX")
else
    mapfile -t candidates < <(echo g++; versioned g++; echo clang++; versioned clang++)
fi

tried=()
for name in "${candidates[@]}"; do
    cxx="$(command -v "$name" || true)"
    if [ -z "$cxx" ]; then
        tried+=("$name: not found")
        continue
    fi
    if ! probe "$cxx"; then
        tried+=("$cxx: no C++20 support ($(grep -m1 -i 'error' "${TMP_DIR}/probe.log" || echo 'probe failed'))")
        continue
    fi
    cc="${CC:-$(c_compiler_for "$cxx")}"
    cc="$(command -v "$cc" || true)"
    if [ -z "$cc" ]; then
        tried+=("$cxx: no matching C compiler found (set CC)")
        continue
    fi
    printf '%s\n' "-DCMAKE_CXX_COMPILER=${cxx}" "-DCMAKE_C_COMPILER=${cc}"
    exit 0
done

echo "error: no C++ compiler with C++20 support found. Tried:" >&2
for t in "${tried[@]}"; do echo "    $t" >&2; done
echo "       Install GCC >= 10 or Clang >= 14 (with a C++20 standard library)," >&2
echo "       or point CXX/CC at one." >&2
exit 1
