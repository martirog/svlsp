#!/usr/bin/env bash
# test_11_rename.sh — verify textDocument/rename (plan.md item 3).
#
# Built on the same lexical, cross-file text search as references -- see
# RenameProvider's own doc comment. Uses the same fixture/positions as
# test_07_references.sh: `rrsh_data` declared at line 16 (0-based) col 8,
# used twice more.

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/ref_rename_sighelp.sv"

section "rename (textDocument/rename)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "null when the cursor isn't on an identifier" \
        "an invalid new name is rejected with an error, not a broken edit" \
        "renaming applies to every occurrence including the declaration"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "null when the cursor isn't on an identifier" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/rename\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 14 :character 0)
                                                 :newName      \"whatever\"))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "an invalid new name is rejected with an error, not a broken edit" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (got-error nil))
             (when ok
               (with-current-buffer buf
                 (condition-case req-err
                     (lsp-request \"textDocument/rename\"
                                  (list :textDocument (list :uri (lsp--buffer-uri))
                                        :position     (list :line 16 :character 8)
                                        :newName      \"1_not_legal\"))
                   (error (setq got-error t)))))
             (svlsp-test/close-file buf)
             (if (and ok got-error) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "renaming applies to every occurrence including the declaration" \
        "(condition-case err
           (let* ((buf  (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok   (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (edit (when ok
                          (with-current-buffer buf
                            (lsp-request \"textDocument/rename\"
                                         (list :textDocument (list :uri (lsp--buffer-uri))
                                               :position     (list :line 16 :character 8)
                                               :newName      \"rrsh_value\")))))
                  (applied-text
                    (when edit
                      (with-current-buffer buf
                        (lsp--apply-workspace-edit edit)
                        (buffer-string)))))
             (svlsp-test/close-file buf)
             (if (and ok edit applied-text
                       (not (string-match-p \"\\\\brrsh_data\\\\b\" applied-text))
                       (string-match-p \"logic rrsh_value;\" applied-text)
                       (string-match-p \"assign rrsh_result = rrsh_value;\" applied-text)
                       (string-match-p \"assign rrsh_value = 1;\" applied-text))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
