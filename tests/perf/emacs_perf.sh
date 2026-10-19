#!/usr/bin/env bash
# emacs_perf.sh -- opt-in latency benchmark through Emacs + lsp-mode.
#
# Not a test_*.sh, so the default suite doesn't pick it up. Run it with
#   make perf-emacs                      (SCENARIO=small|uvm, default uvm)
# or
#   bash tools/emacs-test-daemon.sh tests/perf/emacs_perf.sh
#
# Inputs (environment):
#   SVLSP_PERF_SCENARIO   tests/perf/<name>.json (default uvm)
#   SVLSP_PERF_REPEAT     timed runs per request (default 10, after 1 warm-up)
#   SVLSP_PERF_INTERVAL   ms between typed characters (default 150)
#   SVLSP_PERF_ONLY       regex: only these requests (skips edit and typing)
#   SVLSP_PERF_JSON       where the raw samples go (default a temp file)
#   SVLSP_PERF_SETUP      an elisp file loaded before the run, to time other
#                         lsp-mode settings (e.g. lsp-headerline-breadcrumb-enable)
#   SVLSP_UVM_DB          library DB for the uvm scenario
#   SVLSP_BIN             server under test (`make perf-emacs` uses release)
#
# The workspace is written by `tools/lsp_perf.py --prepare-only`, so this
# and the standalone run (`make perf-lsp`) time the same requests; the
# table is printed by `tools/lsp_perf.py --report`.

PERF_DIR="${SVLSP_ROOT}/tests/perf"
PERF_SCENARIO="${PERF_DIR}/${SVLSP_PERF_SCENARIO:-uvm}.json"
PERF_REPEAT="${SVLSP_PERF_REPEAT:-10}"
PERF_INTERVAL="${SVLSP_PERF_INTERVAL:-150}"
PERF_ONLY="${SVLSP_PERF_ONLY:-}"

section "Emacs latency benchmark ($(basename "${PERF_SCENARIO}" .json))"

PERF_WORK="$(mktemp -d /tmp/svlsp-perf-emacs.XXXXXX)"
PERF_JSON="${SVLSP_PERF_JSON:-${PERF_WORK}/emacs_perf.json}"
if ! PERF_SKIP="$(python3 "${SVLSP_ROOT}/tools/lsp_perf.py" "${PERF_SCENARIO}" \
                      --prepare-only "${PERF_WORK}" 2>&1)"; then
    skip_test "Emacs latency benchmark" "${PERF_SKIP}"
else
    _emacs_eval "(progn (load \"${PERF_DIR}/emacs_perf.el\" nil t) (when (getenv \"SVLSP_PERF_SETUP\") (load (getenv \"SVLSP_PERF_SETUP\") nil t)) t)" >/dev/null
    ONLY_ARG="nil"
    [ -n "${PERF_ONLY}" ] && ONLY_ARG="\"${PERF_ONLY}\""
    run_test "benchmark runs (server ${SVLSP_BIN})" \
        "(svlsp-perf/run \"${PERF_SCENARIO}\" \"${PERF_WORK}\" \"${PERF_JSON}\" ${PERF_REPEAT} 1 ${PERF_INTERVAL} ${ONLY_ARG})" "t"
    if [ -f "${PERF_JSON}" ]; then
        python3 "${SVLSP_ROOT}/tools/lsp_perf.py" --report "${PERF_JSON}"
    fi
fi
if [ -z "${SVLSP_PERF_JSON:-}" ]; then rm -rf "${PERF_WORK}"; fi
