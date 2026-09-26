#!/usr/bin/env bash
# test_41_macro_navigation.sh — real-client round trips for hover,
# go-to-definition and find-references on a `define macro (plan.md §6.29
# follow-up): the macro comes from an included, `define-only header, and a
# same-named parameter in the using module must stay a separate symbol.
#
# Fixtures: fixtures/macro_nav.sv `include`s fixtures/macro_nav.svh.
# Positions are 0-based (line, character), as sent/returned over LSP:
#   macro_nav.svh 1:8   `define MNAV_LOG(ID, MSG="") ...
#   macro_nav.sv  6:16  parameter int MNAV_LOG = 1;
#   macro_nav.sv  8:5   `MNAV_LOG("a", "b");
#   macro_nav.sv  9:5   `MNAV_LOG("c");

FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/macro_nav.sv"

section "macro hover, definition and references"

# refs_test <name> <line> <char> <expected, space-separated sorted basename:line:char>
refs_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/references\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line $2 :character $3)
                                                 :context      (list :includeDeclaration t))))))
                  (got    (mapcar (lambda (loc)
                                    (let ((start (gethash \"start\" (gethash \"range\" loc))))
                                      (format \"%s:%s:%s\"
                                              (file-name-nondirectory
                                               (lsp--uri-to-path (gethash \"uri\" loc)))
                                              (gethash \"line\" start)
                                              (gethash \"character\" start))))
                                  result)))
             (svlsp-test/close-file buf)
             (mapconcat #'identity (sort got #'string<) \" \"))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "\"$4\""
}

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "hover on a macro use shows its signature and body" \
        "definition on a macro use lands on the define in the included header" \
        "references on a macro use: the define and both uses, not the parameter" \
        "references on the same-named parameter: only the parameter"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "hover on a macro use shows its signature and body" \
        "(condition-case err
           (let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/hover\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 8 :character 5))))))
                  (value  (when result (gethash \"value\" (gethash \"contents\" result)))))
             (svlsp-test/close-file buf)
             (if (and value
                      (string-match-p (regexp-quote \"**Macro**\") value)
                      (string-match-p (regexp-quote \"\`MNAV_LOG(ID, MSG = \\\"\\\")\") value)
                      (string-match-p (regexp-quote \"\$display\") value))
                 t
               value))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "definition on a macro use lands on the define in the included header" \
        "(condition-case err
           (let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/definition\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 9 :character 5))))))
                  (loc    (if (hash-table-p result) result (car result)))
                  (start  (when loc (gethash \"start\" (gethash \"range\" loc)))))
             (svlsp-test/close-file buf)
             (when loc
               (format \"%s:%s:%s\"
                       (file-name-nondirectory (lsp--uri-to-path (gethash \"uri\" loc)))
                       (gethash \"line\" start)
                       (gethash \"character\" start))))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "\"macro_nav.svh:1:8\""

    refs_test "references on a macro use: the define and both uses, not the parameter" \
        8 5 "macro_nav.sv:8:5 macro_nav.sv:9:5 macro_nav.svh:1:8"

    refs_test "references on the same-named parameter: only the parameter" \
        6 16 "macro_nav.sv:6:16"
fi
