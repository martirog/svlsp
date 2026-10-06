#!/usr/bin/env bash
# uvm_completion.sh — opt-in: completion against the full UVM library DB.
#
# Not a test_*.sh, so the default suite doesn't pick it up (it needs a UVM
# checkout and a library DB built from it, ~17 min). Run it with
#   make test-integration-uvm
# or
#   bash tools/emacs-test-daemon.sh tests/integration/uvm_corpus/uvm_completion.sh
#
# Inputs (environment):
#   SVLSP_UVM_DB           library DB, schema v13+ (default:
#                          $SVLSP_UVM_CORPUS_DIR/uvm.db). Build it with
#                          svlsp --build-db <corpus>/.svlsp.json --output <db>
#   SVLSP_UVM_CORPUS_DIR   default /home/martin/src/verilator_test/uvm-core/src
#   SVLSP_UVM_TIMEOUT      seconds to wait for the library to load (default 300)
#   SVLSP_UVM_PERF_LOG     where per-request timings go (default a temp file;
#                          printed at the end either way)
#   SVLSP_BIN              server under test (emacs-test-daemon.sh; default the
#                          debug build -- use build/release/svlsp to profile)
#
# The fixture (uvm_tb.sv: a uvm_component subclass in a package, and a
# module) is copied into a temp directory with a generated .svlsp.json
# attaching the library DB. That directory is its own workspace folder, so
# it gets its own svlsp process (see uvm_completion.el).

UVM_DIR="${SVLSP_ROOT}/tests/integration/uvm_corpus"
UVM_CORPUS="${SVLSP_UVM_CORPUS_DIR:-/home/martin/src/verilator_test/uvm-core/src}"
UVM_DB="${SVLSP_UVM_DB:-${UVM_CORPUS}/uvm.db}"
UVM_TIMEOUT="${SVLSP_UVM_TIMEOUT:-300}"

section "UVM library completion (opt-in)"

uvm_db_version() {
    python3 -c "import sqlite3,sys; c=sqlite3.connect('file:'+sys.argv[1]+'?mode=ro', uri=True); print(c.execute('select version from schema_version').fetchone()[0])" "$1" 2>/dev/null
}

UVM_SKIP=""
if [ ! -f "${UVM_DB}" ]; then
    UVM_SKIP="no library DB at ${UVM_DB} (set SVLSP_UVM_DB)"
elif [ "$(uvm_db_version "${UVM_DB}")" -lt 13 ] 2>/dev/null || [ -z "$(uvm_db_version "${UVM_DB}")" ]; then
    UVM_SKIP="${UVM_DB} is schema v$(uvm_db_version "${UVM_DB}"), needs v13+ -- rebuild it with --build-db"
fi

if [ -n "${UVM_SKIP}" ]; then
    skip_test "UVM library completion" "${UVM_SKIP}"
else
    UVM_WORK="$(mktemp -d /tmp/svlsp-uvm-completion.XXXXXX)"
    UVM_PERF="${SVLSP_UVM_PERF_LOG:-${UVM_WORK}/perf.log}"
    : > "${UVM_PERF}"
    cp "${UVM_DIR}/uvm_tb.sv" "${UVM_WORK}/"
    printf '{\n  "files": ["uvm_tb.sv"],\n  "libraryDbs": ["%s"]\n}\n' "${UVM_DB}" > "${UVM_WORK}/.svlsp.json"

    _emacs_eval "(progn (load \"${UVM_DIR}/uvm_completion.el\" nil t)
                        (setq svlsp-uvm/perf-log \"${UVM_PERF}\") t)" >/dev/null
    printf '  server: %s\n  library DB: %s\n' "${SVLSP_BIN}" "${UVM_DB}" >> "${UVM_PERF}"

    run_test "fixture opens in its own workspace and sees the library" \
        "(svlsp-uvm/open \"${UVM_WORK}\" ${UVM_TIMEOUT})" "t"

    # Bare-name completion.
    run_test "library class by prefix (uvm_comp)" \
        "(svlsp-uvm/check \"probe-lib: uvm_comp\" '(\"uvm_component\"))" "t"
    run_test "library class by fuzzy pattern (uvmcmpnt)" \
        "(svlsp-uvm/check \"probe-fuzzy: uvmcmpnt\" '(\"uvm_component\"))" "t"
    run_test "inherited library members by prefix (get_)" \
        "(svlsp-uvm/check \"probe-inherited: get_\" '(\"get_name\" \"get_full_name\" \"get_parent\"))" "t"
    run_test "inherited: protected offered, an ancestor's local not (m_inst)" \
        "(svlsp-uvm/check \"probe-access: m_inst\" '(\"m_inst_count\") '(\"m_inst_id\"))" "t"
    run_test "empty prefix inside a uvm_component subclass" \
        "(svlsp-uvm/check \"probe-empty: \" '(\"tbc_peer\" \"build_phase\" \"get_name\" \"uvm_component\") '(\"m_inst_id\" \"m_tr_h\"))" "t"

    # Dot completion.
    run_test "dot on a function argument (phase.)" \
        "(svlsp-uvm/check \"probe-arg: phase.\" '(\"raise_objection\" \"get_state\" \"get_name\"))" "t"
    run_test "dot on a class property, from a subclass (tbc_peer.)" \
        "(svlsp-uvm/check \"probe-prop: tbc_peer.\" '(\"get_children\" \"get_name\" \"m_inst_count\") '(\"m_inst_id\" \"m_tr_h\"))" "t"
    run_test "super. in a uvm_component subclass" \
        "(svlsp-uvm/check \"probe-super: super.\" '(\"build_phase\" \"get_children\" \"m_inst_count\") '(\"m_inst_id\" \"m_tr_h\"))" "t"
    run_test "chained call (this.get_parent().)" \
        "(svlsp-uvm/check \"probe-chain: this.get_parent().\" '(\"get_children\" \"get_name\"))" "t"
    run_test "queue argument offers queue methods (kids.)" \
        "(svlsp-uvm/check \"probe-queue: kids.\" '(\"push_back\" \"size\") '(\"get_name\"))" "t"
    run_test "indexed queue argument offers the element class (kids[0].)" \
        "(svlsp-uvm/check \"probe-elem: kids[0].\" '(\"get_name\" \"get_children\") '(\"push_back\"))" "t"
    run_test "dot from a module: public members only (tbc_comp.)" \
        "(svlsp-uvm/check \"probe-outside: tbc_comp.\" '(\"get_name\" \"get_children\") '(\"m_inst_count\" \"m_inst_id\" \"m_tr_h\"))" "t"

    _emacs_eval "(svlsp-uvm/close \"${UVM_WORK}\")" >/dev/null

    printf '\n  Timings (%s):\n' "${UVM_PERF}"
    cat "${UVM_PERF}"
    [ -z "${SVLSP_UVM_PERF_LOG:-}" ] && rm -rf "${UVM_WORK}"
fi
