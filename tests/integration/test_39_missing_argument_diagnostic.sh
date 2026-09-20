#!/usr/bin/env bash
# test_39_missing_argument_diagnostic.sh — verify the missing-required-
# argument diagnostic (plan.md §6.23) reaches a real Emacs/lsp-mode client
# via the normal didOpen/publishDiagnostics path, not just SymbolDatabase
# directly (already covered at the unit level, test_compilation_controller.cpp
# and test_sv_listener.cpp).
#
# Fixture layout:
#   callargs_valid.sv   — every declared parameter supplied; zero diagnostics.
#   callargs_missing.sv — omits `b` (no default); exactly one diagnostic.

VALID_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/callargs_valid.sv"
MISSING_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/callargs_missing.sv"

section "missing-required-argument diagnostic for bare function/task calls (plan.md §6.23)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "zero diagnostics when every required argument is supplied" \
        "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "one diagnostic naming the missing parameter and callee" \
        "svlsp binary not found at ${SVLSP_BIN}"
else
    run_test "zero diagnostics when every required argument is supplied" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${VALID_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (count
                    (when ok
                      (with-current-buffer buf
                        (sit-for 2)
                        (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             (if (and ok (eql count 0)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "one diagnostic naming the missing parameter and callee" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${MISSING_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (msg
                    (when ok
                      (with-current-buffer buf
                        (sit-for 2)
                        (let ((diags (lsp--get-buffer-diagnostics)))
                          (and (eql (length diags) 1)
                               (lsp:diagnostic-message (car diags))))))))
             (svlsp-test/close-file buf)
             (if (and ok msg
                      (string-match-p \"'b'\" msg)
                      (string-match-p \"'callargs_missing_func'\" msg))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
