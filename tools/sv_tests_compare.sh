#!/usr/bin/env bash
# Compare two grammars on sv-tests (github.com/chipsalliance/sv-tests): which
# test files each one parses without a syntax error. A file the old grammar
# parses and the new one doesn't is a regression, whatever project would hit
# it -- run this before committing a grammar change (plan.md §6.39).
#
#   tools/sv_tests_compare.sh [--old GRAMMAR] [--new GRAMMAR] [--sll]
#                             [--sv-tests DIR]
#
#   --old       the grammar to compare against (default: grammar/Sv.g4 at
#               git HEAD)
#   --new       the grammar under test (default: grammar/Sv.g4 as it is)
#   --sll       parse with SLL prediction only (svlsp's fast first stage);
#               default LL, what decides svlsp's diagnostics
#   --sv-tests  the sv-tests checkout (default: $SVLSP_SV_TESTS_DIR or
#               ~/src/sv-tests; `git clone --depth 1` is enough -- the
#               hand-written tests/ need no submodules)
#
# Each tests/**/*.sv is preprocessed once with svlsp's preprocessor (pp_dump,
# -I its own directory and UVM's src/, as grammar_profile.sh finds it) and parsed by both grammars (grammar_profile --list,
# one process per grammar). Files the preprocessor rejects (missing
# includes) are skipped and counted. Files
# marked `:should_fail_because:` are reported apart: for them a new syntax
# error is not a regression unless the old grammar parsed them too.
# Output and per-file results: build/sv_tests/.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sv_tests="${SVLSP_SV_TESTS_DIR:-$HOME/src/sv-tests}"
uvm="${SVLSP_UVM_CORPUS_DIR:-/home/martin/src/verilator_test/uvm-core/src}"
out="$root/build/sv_tests"
old="" new="$root/grammar/Sv.g4" mode=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --old)      old="$(realpath "$2")"; shift 2 ;;
        --new)      new="$(realpath "$2")"; shift 2 ;;
        --sll)      mode=(--sll); shift ;;
        --sv-tests) sv_tests="$(realpath "$2")"; shift 2 ;;
        *)          echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[[ -d "$sv_tests/tests" ]] || {
    echo "no sv-tests at $sv_tests (git clone --depth 1 https://github.com/chipsalliance/sv-tests)" >&2
    exit 1
}
mkdir -p "$out"
if [[ -z "$old" ]]; then
    old="$out/Sv_HEAD.g4"
    git -C "$root" show HEAD:grammar/Sv.g4 > "$old"
fi

# Build both parsers (and pp_dump) up front: an empty list parses nothing.
: > "$out/empty.list"
for g in "$old" "$new"; do
    "$root/tools/grammar_profile.sh" --grammar "$g" --input "$out/empty.list" -- --list >/dev/null
done
pp_dump="$root/build/grammar_profile/main/pp_dump"

# Preprocess once per sv-tests checkout state.
rev="$(git -C "$sv_tests" rev-parse HEAD 2>/dev/null || echo unknown) $uvm"
if [[ "$(cat "$out/pp.rev" 2>/dev/null)" != "$rev" ]]; then
    rm -rf "$out/pp"
    : > "$out/pp.list"; : > "$out/pp_failed.list"; : > "$out/should_fail.list"
    while IFS= read -r -d '' f; do
        rel="${f#"$sv_tests"/}"
        dst="$out/pp/$rel.pp.sv"
        mkdir -p "$(dirname "$dst")"
        if "$pp_dump" -I "$(dirname "$f")" -I "$uvm" "$f" > "$dst" 2> "$dst.err" && [[ ! -s "$dst.err" ]]; then
            echo "$dst" >> "$out/pp.list"
        else
            echo "$rel" >> "$out/pp_failed.list"
        fi
        grep -q ':should_fail_because:' "$f" && echo "$dst" >> "$out/should_fail.list"
    done < <(find "$sv_tests/tests" -name '*.sv' -print0 | sort -z)
    echo "$rev" > "$out/pp.rev"
fi

run() { "$root/tools/grammar_profile.sh" --grammar "$1" --input "$out/pp.list" -- --list "${mode[@]}"; }
run "$old" > "$out/old.tsv"
run "$new" > "$out/new.tsv"

python3 - "$out" <<'EOF'
import sys
out = sys.argv[1]
def load(name):
    return {p: int(n) for n, p in (l.rstrip("\n").split("\t", 1) for l in open(f"{out}/{name}"))}
old, new = load("old.tsv"), load("new.tsv")
should_fail = set(l.strip() for l in open(f"{out}/should_fail.list"))
skipped = sum(1 for _ in open(f"{out}/pp_failed.list"))
short = lambda p: p.split("/build/sv_tests/pp/", 1)[-1][:-len(".pp.sv")]
groups = {"regressions": [], "fixed": [], "both pass": [], "both fail": []}
for p in sorted(old):
    o, n = old[p] == 0, new.get(p, 1) == 0
    key = "both pass" if o and n else "both fail" if not o and not n else "regressions" if o else "fixed"
    groups[key].append(p)
print(f"sv-tests: {len(old)} files parsed, {skipped} skipped (preprocessor), "
      f"{len(should_fail)} marked should_fail")
for key in ("both pass", "both fail", "fixed", "regressions"):
    sf = sum(1 for p in groups[key] if p in should_fail)
    print(f"  {key:12s} {len(groups[key]):5d}   ({sf} should_fail)")
for key in ("regressions", "fixed"):
    for p in groups[key]:
        print(f"  {key[:-1] if key.endswith('s') else key}: {short(p)}  "
              f"(old {old[p]}, new {new.get(p)} errors){'  [should_fail]' if p in should_fail else ''}")
real = [p for p in groups["regressions"] if p not in should_fail]
sys.exit(1 if real else 0)
EOF
