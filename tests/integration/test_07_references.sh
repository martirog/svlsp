#!/usr/bin/env bash
# test_07_references.sh — verify textDocument/references (plan.md item 3).
#
# There is no reference-tracking table in this schema, only declarations
# (see ReferencesProvider's own doc comment) -- this is a lexical, cross-file
# text search for the identifier under the cursor, not a scope-aware one.
#
# Fixture: fixtures/ref_rename_sighelp.sv -- `rrsh_data` is declared once
# (line 16, 0-based, col 8) and used twice ("assign rrsh_result =
# rrsh_data;" and "assign rrsh_data = 1;").

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/ref_rename_sighelp.sv"

section "find references (textDocument/references)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "null when the cursor isn't on an identifier" \
        "excludes the declaration by default" \
        "includes the declaration when requested"
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
                              (lsp-request \"textDocument/references\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 14 :character 0)
                                                 :context      (list :includeDeclaration t)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "excludes the declaration by default" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/references\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 16 :character 8)
                                                 :context      (list :includeDeclaration :json-false)))))))
             (svlsp-test/close-file buf)
             (if (and ok (listp result) (eql (length result) 2)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "includes the declaration when requested" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/references\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 16 :character 8)
                                                 :context      (list :includeDeclaration t)))))))
             (svlsp-test/close-file buf)
             (if (and ok (listp result) (eql (length result) 3)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
