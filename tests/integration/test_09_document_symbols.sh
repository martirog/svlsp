#!/usr/bin/env bash
# test_09_document_symbols.sh — verify that svlsp handles
# textDocument/documentSymbol without crashing and returns a null result
# (pre-ANTLR4, no symbol outline yet).
#
# Phase 3.7: the document-symbol handler is wired and routing works, but the
# server has no parser, so it always responds with JSON null.
# Phase 4 will return module, interface, function, and task declarations.
#
# lsp-request sends a synchronous textDocument/documentSymbol RPC.  JSON null
# maps to Emacs nil, so a null response is verified with (null result).

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "document symbols (textDocument/documentSymbol)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after documentSymbol request" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "documentSymbol returns null (pre-ANTLR4)"  "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a documentSymbol request -------------------------
    run_test "server alive after documentSymbol request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"textDocument/documentSymbol\"
                                        (list :textDocument (list :uri (lsp--buffer-uri)))))
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- null result (no parser yet) --------------------------------------
    run_test "documentSymbol returns null (pre-ANTLR4)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/documentSymbol\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
