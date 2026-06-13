#!/usr/bin/env bash
# test_09_document_symbols.sh — verify that textDocument/documentSymbol returns
# the real symbol outline from the DB-backed provider (Phase 6).
#
# module_basic.sv contains module adder with ports and a parameter.  After
# didOpen the server parses the file and stores symbols.  A documentSymbol
# request should return a non-null array of DocumentSymbol objects.
# An unknown file URI returns null.

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "document symbols (textDocument/documentSymbol)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after documentSymbol request"          "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "documentSymbol returns symbols for open file"       "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "documentSymbol returns null for unknown file"       "svlsp binary not found at ${SVLSP_BIN}"
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

    # --- documentSymbol returns a non-null list for an open file ----------
    run_test "documentSymbol returns symbols for open file" \
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
             (if (and ok (not (null result))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- documentSymbol returns null for a file not in the DB -------------
    run_test "documentSymbol returns null for unknown file" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/documentSymbol\"
                                      (list :textDocument
                                            (list :uri \"file:///nonexistent/unknown.sv\")))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
