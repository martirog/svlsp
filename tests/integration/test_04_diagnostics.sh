#!/usr/bin/env bash
# test_04_diagnostics.sh — verify that svlsp pushes textDocument/publishDiagnostics
# with an empty list when a document is opened or changed.
#
# At this stage (pre-ANTLR4) the server has no parser, so it always publishes
# empty diagnostics.  The functional test verifies that:
#   1. No errors appear in Emacs after opening a clean fixture.
#   2. No errors appear after a buffer modification (didChange).
#
# lsp-mode stores received diagnostics via lsp--workspace-diagnostics, queried
# with (lsp--get-buffer-diagnostics) from within the buffer context.
# We sit-for 2s to allow the publishDiagnostics notification to arrive.

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "diagnostics (publishDiagnostics)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "zero diagnostics after didOpen"   "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "zero diagnostics after didChange" "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- empty diagnostics on didOpen -------------------------------------
    run_test "zero diagnostics after didOpen" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok  (with-current-buffer buf
                          (svlsp-test/wait-for-lsp 15)))
                   (count
                     (when ok
                       (with-current-buffer buf
                         (sit-for 2)
                         (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             (if (and ok (eql count 0)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- empty diagnostics on didChange -----------------------------------
    run_test "zero diagnostics after didChange" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok  (with-current-buffer buf
                          (svlsp-test/wait-for-lsp 15)))
                   (count
                     (when ok
                       (with-current-buffer buf
                         (goto-char (point-max))
                         (insert \"\\n\")
                         (sit-for 3)
                         (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             (if (and ok (eql count 0)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
