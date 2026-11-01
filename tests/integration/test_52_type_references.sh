#!/usr/bin/env bash
# test_52_type_references.sh — the declared-type warning (plan.md §6.21
# step 3) through lsp-mode: in a project (fixtures/typeref_proj/.svlsp.f),
# a type no visible declaration covers gets one Warning at its name, an
# imported one gets none, and editing the name to a visible type clears it.
#
# Fixture: fixtures/typeref_proj/tref_use.sv. Positions are 0-based.

TREF_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/typeref_proj/tref_use.sv"

section "declared-type warnings (plan.md §6.21 step 3)"

# tref_test <name> <form over the opened buffer> <expected>
tref_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${TREF_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (got (when ok (with-current-buffer buf $2))))
             (svlsp-test/close-file buf)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$3"
}

# Waits up to SECS for the buffer's diagnostic count to satisfy PRED.
WAIT_DIAGS="(lambda (pred secs)
              (let ((deadline (+ (float-time) secs)))
                (while (and (not (funcall pred (length (lsp--get-buffer-diagnostics))))
                            (< (float-time) deadline))
                  (sit-for 0.1))))"

tref_test "type references: one Warning at the type nothing declares" \
    "(progn
       (funcall ${WAIT_DIAGS} (lambda (n) (>= n 1)) 15)
       (sit-for 0.5)
       (mapconcat
        (lambda (d)
          (let ((start (lsp:range-start (lsp:diagnostic-range d))))
            (format \"%s:%s:%s:%s\" (lsp:position-line start) (lsp:position-character start)
                    (lsp:diagnostic-severity? d) (lsp:diagnostic-message d))))
        (lsp--get-buffer-diagnostics) \" | \"))" \
    "\"6:2:2:unknown type 'tref_missing'\""

tref_test "type references: naming a visible type clears the Warning" \
    "(progn
       (funcall ${WAIT_DIAGS} (lambda (n) (>= n 1)) 15)
       (goto-char (point-min))
       (search-forward \"tref_missing bad_item\")
       (replace-match \"tref_item bad_item\")
       (funcall ${WAIT_DIAGS} (lambda (n) (= n 0)) 10)
       (length (lsp--get-buffer-diagnostics)))" \
    "0"
