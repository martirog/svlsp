#!/usr/bin/env bash
# test_08_completion.sh — verify that textDocument/completion returns real
# candidates from the DB-backed completion provider (Phase 6).
#
# module_basic.sv: after didOpen the server parses the file and stores symbols.
# Requesting completion at line=10, char=11 (inside "assign sum = a + b;",
# cursor on "sum") is inside the "adder" module scope.  findSymbolsVisibleAt
# should return ports/parameters visible there, so the result is non-null.
# Requesting completion at line=2, char=0 (blank line, above the module
# declaration) still returns the file's top-level symbol(s) — here, the
# module "adder" itself. findSymbolsVisibleAt's global scope ("") branch
# returns every same-file top-level symbol regardless of whether the query
# line falls before or after that symbol's own declaration line — SV modules
# aren't subject to a forward-declaration rule, and this matches the same
# no-forward-reference-filtering behavior test_16_class_scope_completion.sh
# already establishes for members *within* a scope (e.g. a class method is
# visible even on a line before the method's own declaration).

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"
COMMENT_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/completion_in_comment.sv"

section "completion (textDocument/completion)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after completion request"          "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "completion returns candidates inside module"    "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "completion at top-level scope includes the module itself"   "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "completion inside a comment or string offers nothing"   "svlsp binary not found at ${SVLSP_BIN}"
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

    # --- completion at top-level scope includes the module itself ---------
    run_test "completion at top-level scope includes the module itself" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/completion\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 2 :character 0))))))
                   (items  (when (hash-table-p result) (gethash \"items\" result)))
                   (labels (when (listp items)
                             (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"adder\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "completion inside a comment or string offers nothing" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${COMMENT_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (at  (lambda (line char)
                         (with-current-buffer buf
                           (lsp-request \"textDocument/completion\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line line :character char))))))
                  (in-comment (when ok (funcall at 7 27)))
                  (in-string  (when ok (funcall at 8 25)))
                  (in-code    (when ok (funcall at 7 11)))
                  (items      (when (hash-table-p in-code) (gethash \"items\" in-code)))
                  (labels     (mapcar (lambda (i) (gethash \"label\" i)) items)))
             (svlsp-test/close-file buf)
             (if (and ok (null in-comment) (null in-string) (member \"cmtc_other\" labels)) t
               (format \"comment=%S string=%S code=%S\" in-comment in-string labels)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
