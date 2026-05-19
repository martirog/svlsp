#!/usr/bin/env bash
# test_03_document_sync.sh — verify that svlsp handles textDocument/didOpen,
# textDocument/didChange, and textDocument/didClose without crashing.
#
# lsp-mode sends didOpen when a buffer opens, didChange on modification,
# and didClose when the buffer is killed.  We verify the server stays in the
# 'initialized' state throughout each operation.
#
# Note: lsp-workspaces is buffer-local, so all workspace status checks
# must occur inside (with-current-buffer buf ...).

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"
SV_FIXTURE2="${SVLSP_ROOT}/examples/module_params.sv"

section "document synchronisation (didOpen / didChange / didClose)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after didOpen"   "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "server alive after didChange" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "server alive after didClose"  "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- didOpen -----------------------------------------------------------
    # Open the fixture, wait for the initialize handshake to complete, verify
    # the workspace is still initialized, then close the buffer.
    run_test "server alive after didOpen" \
        "(let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                 (ok  (with-current-buffer buf
                        (svlsp-test/wait-for-lsp 15)))
                 (alive (when ok
                          (with-current-buffer buf
                            (cl-some (lambda (ws)
                                       (eq (lsp--workspace-status ws) 'initialized))
                                     (lsp-workspaces))))))
           (svlsp-test/close-file buf)
           (if alive t nil))" \
        "t"

    # --- didChange ---------------------------------------------------------
    # Open, wait for LSP, insert a character (triggers didChange via lsp-mode's
    # idle timer), verify the workspace is still initialized in buffer context.
    run_test "server alive after didChange" \
        "(let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                 (ok  (with-current-buffer buf
                        (svlsp-test/wait-for-lsp 15)))
                 (alive
                   (when ok
                     (with-current-buffer buf
                       (goto-char (point-max))
                       (insert \"\\n\")
                       (sit-for 2)
                       (cl-some (lambda (ws)
                                  (eq (lsp--workspace-status ws) 'initialized))
                                (lsp-workspaces))))))
           (svlsp-test/close-file buf)
           (if alive t nil))" \
        "t"

    # --- didClose ----------------------------------------------------------
    # Open two files (both tracked by the server).  Close the first — the server
    # receives didClose for it.  The second file's workspace must still be
    # initialized, proving the server handled didClose without crashing.
    run_test "server alive after didClose" \
        "(let* ((buf1 (svlsp-test/open-file \"${SV_FIXTURE}\"))
                 (buf2 (svlsp-test/open-file \"${SV_FIXTURE2}\"))
                 (ok   (with-current-buffer buf2
                         (svlsp-test/wait-for-lsp 15))))
           (svlsp-test/close-file buf1)
           (sit-for 1)
           (let ((alive (when ok
                          (with-current-buffer buf2
                            (cl-some (lambda (ws)
                                       (eq (lsp--workspace-status ws) 'initialized))
                                     (lsp-workspaces))))))
             (svlsp-test/close-file buf2)
             (if alive t nil)))" \
        "t"
fi
