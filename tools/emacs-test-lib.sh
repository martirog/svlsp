#!/usr/bin/env bash
# emacs-test-lib.sh — shared helpers sourced by every integration test file.
#
# Expects the following variables to be set by emacs-test-daemon.sh:
#   DAEMON_NAME   — Emacs daemon socket name
#   SVLSP_ROOT    — absolute project root

PASS=0
FAIL=0
SKIP=0

# Send an Elisp expression to the running daemon and return its printed value.
_emacs_eval() {
    emacsclient -s "${DAEMON_NAME}" -e "$1" 2>/dev/null
}

# run_test <description> <elisp-expression> <expected-output>
#
# Evaluates <elisp-expression> via emacsclient and compares the result
# (as a string) to <expected-output>. Increments PASS or FAIL.
run_test() {
    local name="$1"
    local elisp="$2"
    local expected="$3"

    local result
    result=$(_emacs_eval "$elisp" | tr -d '\n')

    if [ "$result" = "$expected" ]; then
        printf "  PASS  %s\n" "$name"
        PASS=$((PASS + 1))
    else
        printf "  FAIL  %s\n" "$name"
        printf "        expected: %s\n" "$expected"
        printf "        got:      %s\n" "$result"
        FAIL=$((FAIL + 1))
    fi
}

# skip_test <description> <reason>
skip_test() {
    printf "  SKIP  %s  (%s)\n" "$1" "$2"
    SKIP=$((SKIP + 1))
}

# Print a section header.
section() {
    printf "\n=== %s ===\n" "$1"
}

# Called at the end of emacs-test-daemon.sh to print the final tally.
print_summary() {
    local sep="============================="
    printf "\n%s\n" "$sep"
    printf "  PASSED:  %d\n" "$PASS"
    printf "  FAILED:  %d\n" "$FAIL"
    printf "  SKIPPED: %d\n" "$SKIP"
    printf "%s\n" "$sep"
    [ "$FAIL" -eq 0 ]
}
