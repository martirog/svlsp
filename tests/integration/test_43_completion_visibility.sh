#!/usr/bin/env bash
# test_43_completion_visibility.sh — negative completion checks through the
# real client: what bare-name completion must NOT offer. Complements the
# unit suite tests/unit/lsp/test_completion_visibility.cpp.
#
# Fixture: tests/integration/fixtures/completion_visibility.sv. Each probe
# is a blank line (empty prefix, so the whole list is checked):
#   LSP line 19 -- in module cvis_imp   (import cvis_p::*)
#   LSP line 23 -- in module cvis_none  (no import; cvis_imp's must not leak)
#   LSP line 28 -- in module cvis_trans (import cvis_q::*, which imports
#                  cvis_p without exporting it)
#   LSP line 33 -- in CvisC::cvis_cf's body

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/completion_visibility.sv"

TESTS=(
    "visibility: an imported package's items, not its class's members or another package's"
    "visibility: an import in another module of the file doesn't leak"
    "visibility: a plain import is not transitive"
    "visibility: a class method sees no package item, no other class's method, no class/function keyword"
)

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in "${TESTS[@]}"; do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "negative completion visibility (textDocument/completion)"

# vis_test <name> <line> <labels-predicate over `labels`>
vis_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line $2 :character 0))))))
                  (items  (cond ((hash-table-p result) (gethash \"items\" result))
                                ((listp result) result)))
                  (labels (mapcar (lambda (i) (gethash \"label\" i)) items)))
             (svlsp-test/close-file buf)
             (if (and ok labels $3) t (format \"labels: %S\" labels)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
}

vis_test "${TESTS[0]}" 19 \
    "(member \"cvis_pf\" labels) (member \"CvisPC\" labels)
     (not (member \"cvis_pcm\" labels)) (not (member \"cvis_qf\" labels))"
vis_test "${TESTS[1]}" 23 \
    "(not (member \"cvis_pf\" labels)) (not (member \"CvisPC\" labels))
     (not (member \"cvis_qf\" labels))"
vis_test "${TESTS[2]}" 28 \
    "(member \"cvis_qf\" labels)
     (not (member \"cvis_pf\" labels)) (not (member \"CvisPC\" labels))"
vis_test "${TESTS[3]}" 33 \
    "(member \"cvis_cf\" labels)
     (not (member \"cvis_pf\" labels)) (not (member \"cvis_pcm\" labels))
     (not (member \"class\" labels)) (not (member \"function\" labels))
     (not (member \"task\" labels))"
