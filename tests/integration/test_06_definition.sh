#!/usr/bin/env bash
# test_06_definition.sh — verify that textDocument/definition returns a real
# Location from the DB-backed definition provider (Phase 6).
#
# module_basic.sv has "module adder" at line 4 (1-based), column 7 (0-based).
# After didOpen the server parses the file and stores symbols.  Go-to-definition
# on "adder" (LSP line=3, char=9) should return a non-null Location pointing
# to the same file.  A position on whitespace (line=2, char=0) returns null.

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "go-to-definition (textDocument/definition)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after definition request"       "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "definition returns location for adder"       "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "definition returns null on non-identifier"   "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a definition request -----------------------------
    run_test "server alive after definition request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"textDocument/definition\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 0 :character 0))))
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- definition for "adder" returns a non-null Location ---------------
    # LSP position: line=3 (0-based), character=9 (inside "adder")
    run_test "definition returns location for adder" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/definition\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 3 :character 9)))))))
             (svlsp-test/close-file buf)
             (if (and ok (not (null result))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- definition on whitespace returns null ----------------------------
    run_test "definition returns null on non-identifier" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/definition\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 2 :character 0)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
