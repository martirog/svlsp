#!/usr/bin/env bash
# test_07_references.sh — verify that svlsp handles textDocument/references
# without crashing and returns a null result (pre-ANTLR4, no symbol DB yet).
#
# Phase 3.5: the references handler is wired and routing works, but the server
# has no parser or database, so it always responds with JSON null.
# Phase 4 will return all source locations that reference a given symbol.
#
# lsp-request sends a synchronous textDocument/references RPC.  JSON null maps
# to Emacs nil, so a null response is verified with (null result).

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "find references (textDocument/references)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after references request" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "references returns null (pre-ANTLR4)"  "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a references request -----------------------------
    run_test "server alive after references request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"textDocument/references\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 0 :character 0)
                                              :context      (list :includeDeclaration t))))
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- null result (no symbol DB yet) -----------------------------------
    run_test "references returns null (pre-ANTLR4)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/references\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 0 :character 5)
                                            :context      (list :includeDeclaration :json-false)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
