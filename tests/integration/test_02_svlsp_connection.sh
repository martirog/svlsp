#!/usr/bin/env bash
# test_02_svlsp_connection.sh — verify that Emacs lsp-mode can connect to
# the svlsp binary and complete the LSP initialize handshake.
#
# Prerequisite: SVLSP_BIN must point to a built svlsp binary.

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "svlsp connection (initialize handshake)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "svlsp connects to Emacs" "svlsp binary not found at ${SVLSP_BIN}"
else
    # Open the SystemVerilog fixture with verilog-mode, connect lsp-mode to
    # svlsp, wait up to 15 s for the initialize handshake to complete.
    run_test "svlsp connects and initializes" \
        "(let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                 (ok  (with-current-buffer buf
                        (svlsp-test/wait-for-lsp 15))))
           (svlsp-test/close-file buf)
           (if ok t nil))" \
        "t"

    run_test "svlsp server-id is reported as svlsp" \
        "(let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                 (id  (with-current-buffer buf
                        (svlsp-test/wait-for-lsp 15)
                        (let ((ws (car (lsp-workspaces))))
                          (when ws (lsp--workspace-server-id ws))))))
           (svlsp-test/close-file buf)
           (eq id 'svlsp))" \
        "t"
fi
