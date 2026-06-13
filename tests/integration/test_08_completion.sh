#!/usr/bin/env bash
# test_08_completion.sh — verify that textDocument/completion returns real
# candidates from the DB-backed completion provider (Phase 6).
#
# module_basic.sv: after didOpen the server parses the file and stores symbols.
# Requesting completion at line=10, char=11 (inside "assign sum = a + b;",
# cursor on "sum") is inside the "adder" module scope.  findSymbolsVisibleAt
# should return ports/parameters visible there, so the result is non-null.
# Requesting completion at line=2, char=0 (blank line, not inside any scope)
# returns null because no symbols are visible at that position.

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "completion (textDocument/completion)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after completion request"          "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "completion returns candidates inside module"    "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "completion returns null outside any scope"      "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a completion request -----------------------------
    run_test "server alive after completion request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"textDocument/completion\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 0 :character 0))))
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- completion inside module body returns candidates -----------------
    # line=10 (0-based) = "    assign sum = a + b;", char=11 cursor on "sum"
    run_test "completion returns candidates inside module" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/completion\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 10 :character 11)))))))
             (svlsp-test/close-file buf)
             (if (and ok (not (null result))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- completion on blank line (outside any scope) returns null --------
    run_test "completion returns null outside any scope" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/completion\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 2 :character 0)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
