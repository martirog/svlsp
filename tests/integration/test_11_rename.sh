#!/usr/bin/env bash
# test_11_rename.sh — verify that svlsp handles textDocument/rename without
# crashing and returns a null result (pre-ANTLR4, no symbol DB yet).
#
# Phase 3.9: the rename handler is wired and routing works, but the server has
# no parser or database, so it always responds with JSON null.
# Phase 4 will return a WorkspaceEdit covering all reference sites.
#
# lsp-request sends a synchronous textDocument/rename RPC.  JSON null maps to
# Emacs nil, so a null response is verified with (null result).

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "rename (textDocument/rename)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after rename request" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "rename returns null (pre-ANTLR4)"  "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a rename request ---------------------------------
    run_test "server alive after rename request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"textDocument/rename\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 0 :character 0)
                                              :newName      \"new_signal\"))))
                       (with-current-buffer buf
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- null result (no symbol DB yet) -----------------------------------
    run_test "rename returns null (pre-ANTLR4)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/rename\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 0 :character 5)
                                            :newName      \"renamed_signal\"))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
