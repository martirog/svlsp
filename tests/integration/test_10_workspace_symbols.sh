#!/usr/bin/env bash
# test_10_workspace_symbols.sh — verify that svlsp handles workspace/symbol
# without crashing and returns a null result (pre-ANTLR4, no symbol DB yet).
#
# Phase 3.8: the workspace-symbol handler is wired and routing works, but the
# server has no parser or database, so it always responds with JSON null.
# Phase 4 will search the symbol table across all open files.
#
# lsp-request sends a synchronous workspace/symbol RPC.  JSON null maps to
# Emacs nil, so a null response is verified with (null result).

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "workspace symbols (workspace/symbol)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after workspace/symbol request" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "workspace/symbol returns null (pre-ANTLR4)"  "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a workspace/symbol request -----------------------
    run_test "server alive after workspace/symbol request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"workspace/symbol\"
                                        (list :query \"\"))))
                       (with-current-buffer buf
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- null result (no symbol DB yet) -----------------------------------
    run_test "workspace/symbol returns null (pre-ANTLR4)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"workspace/symbol\"
                                      (list :query \"my_module\"))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
