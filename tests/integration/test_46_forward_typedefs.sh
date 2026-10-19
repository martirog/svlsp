#!/usr/bin/env bash
# test_46_forward_typedefs.sh — declare-before-use (plan.md §6.30,
# "Follow-up: forward typedefs and declare-before-use"): outside a class, a
# name is offered only after its declaration in the compilation unit's
# include order, unless an earlier `typedef class` declares it; the forward
# typedef is a fallback the real class replaces wherever it is visible.
#
# Fixtures (tests/integration/fixtures):
#   fwdtd_top.sv  -- package fwdtd_p: `typedef class fwdtd_late;`, then
#                    includes fwdtd_a.svh and fwdtd_b.svh
#   fwdtd_a.svh   -- class fwdtd_a; LSP line 1 uses fwdtd_late, line 3
#                    declares fwdtd_before, line 4 is blank (col 4), line 5
#                    declares fwdtd_after
#   fwdtd_b.svh   -- class fwdtd_late (line 0), class fwdtd_b

TOP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/fwdtd_top.sv"
A_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/fwdtd_a.svh"

TESTS=(
    "forward typedefs: completion offers only names declared before the cursor, the forward-declared class once"
    "forward typedefs: definition on a forward-declared class lands on the class in a later included file"
    "forward typedefs: document symbols don't list the forward typedef"
)

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in "${TESTS[@]}"; do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "forward typedefs and declare-before-use"

# fwdtd_test <name> <form over `buf` (the opened fwdtd_a.svh)> <expected>
# Opens the includer first, so its compile records the include sites.
fwdtd_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((top (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok1 (with-current-buffer top (svlsp-test/wait-for-lsp 15)))
                  (buf (svlsp-test/open-file \"${A_FIXTURE}\"))
                  (ok2 (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (got (when (and ok1 ok2) (with-current-buffer buf (sit-for 1) $2))))
             (svlsp-test/close-file buf)
             (svlsp-test/close-file top)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$3"
}

fwdtd_test "${TESTS[0]}" \
    "(let* ((result (lsp-request \"textDocument/completion\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position     (list :line 4 :character 4))))
            (items  (cond ((hash-table-p result) (gethash \"items\" result))
                          ((listp result) result)))
            (labels (mapcar (lambda (i) (gethash \"label\" i)) items)))
       (if (and (member \"fwdtd_before\" labels)
                (= 1 (seq-count (lambda (l) (equal l \"fwdtd_late\")) labels))
                (not (member \"fwdtd_after\" labels))
                (not (member \"fwdtd_b\" labels)))
           t
         (format \"labels: %S\" labels)))" \
    "t"

fwdtd_test "${TESTS[1]}" \
    "(let* ((result (lsp-request \"textDocument/definition\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position     (list :line 1 :character 4))))
            (loc    (if (hash-table-p result) result (car result))))
       (when loc
         (let ((start (gethash \"start\" (gethash \"range\" loc))))
           (format \"%s:%s\" (file-name-nondirectory (lsp--uri-to-path (gethash \"uri\" loc)))
                   (gethash \"line\" start)))))" \
    "\"fwdtd_b.svh:0\""

run_test "${TESTS[2]}" \
    "(condition-case err
       (let* ((top (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok  (with-current-buffer top (svlsp-test/wait-for-lsp 15)))
              (got (when ok
                     (with-current-buffer top
                       (let* ((syms  (lsp-request \"textDocument/documentSymbol\"
                                                  (list :textDocument (list :uri (lsp--buffer-uri)))))
                              (names (mapcar (lambda (s) (gethash \"name\" s)) syms)))
                         (if (and (member \"fwdtd_p\" names) (not (member \"fwdtd_late\" names)))
                             t
                           (format \"names: %S\" names)))))))
         (svlsp-test/close-file top)
         got)
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
