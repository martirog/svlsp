#!/usr/bin/env bash
# test_42_doc_comments.sh — real-client round trips for doc comments on
# declarations (plan.md §6.31): hover shows a class's and a macro's doc,
# signature help carries a function's doc, and completionItem/resolve fills
# in a completion item's doc (the completion list itself carries none).
#
# Fixture: fixtures/doc_comments.sv. Positions are 0-based (line, character):
#   4:6   class dcmt_c;           (doc "A documented class." + "Second line.")
#   12:13 dcmt_add(1, 2);         inside the call's parentheses
#   13:5  `DCMT_LOG("x");         (doc "Logs a dcmt message.")
#   17:7  dcmt_                   completion probe (inside an undefined `ifdef)

FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/doc_comments.sv"

section "doc comments (hover, signature help, completion resolve)"

# hover_value <line> <char>: elisp evaluating to the hover's markdown text.
hover_value() {
    echo "(let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/hover\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line $1 :character $2)))))))
             (svlsp-test/close-file buf)
             (when result (gethash \"value\" (gethash \"contents\" result))))"
}

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "hover on a documented class shows its doc" \
        "hover on a documented macro shows its doc" \
        "signature help carries the function's doc" \
        "completion resolve fills in the item's doc"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "hover on a documented class shows its doc" \
        "(condition-case err
           (let ((value $(hover_value 4 6)))
             (if (and value
                      (string-match-p (regexp-quote \"**Class** \`dcmt_c\`\") value)
                      (string-match-p (regexp-quote \"A documented class.  
Second line.\") value))
                 t
               value))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "hover on a documented macro shows its doc" \
        "(condition-case err
           (let ((value $(hover_value 13 5)))
             (if (and value (string-match-p (regexp-quote \"Logs a dcmt message.\") value))
                 t
               value))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "signature help carries the function's doc" \
        "(condition-case err
           (let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 12 :character 13))))))
                  (sig    (when result (aref (gethash \"signatures\" result) 0))))
             (svlsp-test/close-file buf)
             (when sig (gethash \"documentation\" sig)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "\"Adds two numbers.\""

    run_test "completion resolve fills in the item's doc" \
        "(condition-case err
           (let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (items  (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 17 :character 7))))))
                  (list   (if (hash-table-p items) (gethash \"items\" items) items))
                  (item   (seq-find (lambda (it) (equal (gethash \"label\" it) \"dcmt_c\"))
                                    list))
                  (resolved (when item
                              (with-current-buffer buf
                                (lsp-request \"completionItem/resolve\" item))))
                  (doc    (when resolved (gethash \"documentation\" resolved))))
             (svlsp-test/close-file buf)
             (cond ((not item) \"no dcmt_c item\")
                   ((gethash \"documentation\" item) \"doc sent with the list\")
                   ((hash-table-p doc) (gethash \"value\" doc))
                   (t doc)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "\"A documented class.  \\nSecond line.\""
fi
