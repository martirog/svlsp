#!/usr/bin/env bash
# test_05_hover.sh — verify that svlsp handles textDocument/hover without
# crashing and returns a null result (pre-ANTLR4, no symbol information yet).
#
# Phase 3.3: the hover handler is wired and routing works, but the server has
# no parser or database, so it always responds with JSON null.
# Phase 4 will populate real hover text from ANTLR4 symbol extraction.
#
# lsp-request sends a synchronous textDocument/hover RPC.  JSON null maps to
# Emacs nil, so a null response is verified with (null result).

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "hover (textDocument/hover)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after hover request" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "hover returns null (pre-ANTLR4)"  "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a hover request ----------------------------------
    run_test "server alive after hover request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"textDocument/hover\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 0 :character 0))))
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- null result (no symbol DB yet) -----------------------------------
    run_test "hover returns null (pre-ANTLR4)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/hover\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 0 :character 5)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
