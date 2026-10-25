#!/usr/bin/env bash
# Profile the parser generated from a grammar on preprocessed UVM
# (plan.md §6.33 C2). Builds tools/grammar_profile/ for the grammar (once per
# grammar edit), preprocesses UVM once, then runs grammar_profile.
#
#   tools/grammar_profile.sh [--grammar FILE] [--input slice|uvm|FILE] [--walk-dump]
#                            [-- ARGS...]
#
#   --grammar FILE  grammar to profile (default grammar/Sv.g4); each grammar
#                   file gets its own build tree, build/grammar_profile/<name>
#   --input         slice: UVM cut after the first `endclass` at or after
#                   line 12500 (default; the T2 slice); uvm: all of UVM;
#                   or a preprocessed file
#   ARGS            passed to grammar_profile: --sll --profile --top N
#                   --tsv FILE --repeat N --errors N --sort ctx --where D,...
#                   (see profile_main.cpp)
#   --walk-dump     run walk_dump instead (svlsp's walker on this grammar's
#                   parser; prints its records -- diff two grammars' dumps);
#                   ARGS: --ll or --sll (default: svlsp's two-stage parse)
#
# UVM is found at SVLSP_UVM_CORPUS_DIR (default as the corpus tests) and
# preprocessed with UVM_NO_DPI, as the corpus and uvm_src scenarios do. The
# ANTLR runtime comes from build/release (SVLSP_BUILD_DIR to change it).
#
# Examples:
#   tools/grammar_profile.sh -- --profile                 # LL, slice, top decisions
#   tools/grammar_profile.sh --input uvm -- --sll --errors 0
#   tools/grammar_profile.sh --grammar /tmp/Sv_e1.g4 -- --profile --tsv e1.tsv
#   tools/grammar_profile.sh --walk-dump -- --ll > main.txt
#   tools/grammar_profile.sh --grammar /tmp/Sv_e1.g4 --walk-dump -- --sll > e1.txt
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
grammar="$root/grammar/Sv.g4"
input=slice
tool=grammar_profile
while [[ $# -gt 0 ]]; do
    case "$1" in
        --grammar) grammar="$(realpath "$2")"; shift 2 ;;
        --input)   input="$2"; shift 2 ;;
        --walk-dump) tool=walk_dump; shift ;;
        --)        shift; break ;;
        *)         echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

svlsp_build="${SVLSP_BUILD_DIR:-$root/build/release}"
uvm="${SVLSP_UVM_CORPUS_DIR:-/home/martin/src/verilator_test/uvm-core/src}"
out="$root/build/grammar_profile"
name="$(basename "$grammar" .g4)"
[[ "$grammar" == "$root/grammar/Sv.g4" ]] && name=main
build="$out/$name"

cmake -S "$root/tools/grammar_profile" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER="${CXX:-g++-13}" -DCMAKE_AR="$(command -v gcc-ar-13 || command -v ar)" \
    -DCMAKE_RANLIB="$(command -v gcc-ranlib-13 || command -v ranlib)" \
    -DSV_GRAMMAR="$grammar" -DSVLSP_BUILD_DIR="$svlsp_build" >/dev/null
cmake --build "$build" --target "$tool" pp_dump -j"$(nproc)" >/dev/null

pp="$out/uvm.pp.sv"
if [[ ! -s "$pp" ]]; then
    "$build/pp_dump" -I "$uvm" -D UVM_NO_DPI "$uvm/uvm.sv" > "$pp.tmp" 2> "$pp.log"
    mv "$pp.tmp" "$pp"
fi

slice=()
case "$input" in
    slice) file="$pp"; slice=(--slice 12500) ;;
    uvm)   file="$pp" ;;
    *)     file="$input" ;;
esac
exec "$build/$tool" "${slice[@]}" "$@" "$file"
