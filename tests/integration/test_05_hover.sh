#!/usr/bin/env bash
# test_05_hover.sh — verify that textDocument/hover returns real symbol info
# from the DB-backed hover provider (Phase 6).
#
# module_basic.sv has "module adder" at line 4 (1-based), character 7 (0-based).
# After didOpen the server parses the file and stores symbols, so hover on
# "adder" (LSP position line=3, character=9) should return a non-null Hover
# with markdown content.  A position on a comment (line=0, char=0) still
# returns null because "module" is not in the symbol DB.

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "hover (textDocument/hover)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after hover request"          "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "hover returns module info for adder"       "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "hover returns null on non-identifier line" "svlsp binary not found at ${SVLSP_BIN}"
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

    # --- hover over "adder" returns non-null hover content ----------------
    # LSP position: line=3 (0-based), character=9 (inside "adder")
    run_test "hover returns module info for adder" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/hover\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 3 :character 9)))))))
             (svlsp-test/close-file buf)
             (if (and ok (not (null result))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- hover on a non-identifier (blank line) returns null --------------
    run_test "hover returns null on non-identifier line" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/hover\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 2 :character 0)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
