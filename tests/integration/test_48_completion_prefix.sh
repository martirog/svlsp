#!/usr/bin/env bash
# test_48_completion_prefix.sh — the completion prefix is the text before
# the cursor only (plan.md §6.33 F6/C7): at the start of `alpha` or
# `p.|alpha_m` the rest of the word must not filter.
#
# Fixture: completion_prefix.sv (0-based lines)
#   13  "    alpha = 1;"     char 4 = |alpha, char 6 = al|pha
#   14  "    p.alpha_m = 2;" char 6 = p.|alpha_m, char 8 = p.al|pha_m

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/completion_prefix.sv"

section "completion prefix = text before the cursor"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "completion prefix: bare name, cursor at the start of a word offers other names" \
        "completion prefix: bare name, cursor in the middle filters by the text before it" \
        "completion prefix: dot, cursor right after the dot offers all members" \
        "completion prefix: dot, cursor in the middle of a member filters by the text before it"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "completion prefix: bare name, cursor at the start of a word offers other names" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 13 :character 4))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"alpha\" labels) (member \"beta\" labels)) t (format \"%S\" labels)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "completion prefix: bare name, cursor in the middle filters by the text before it" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 13 :character 6))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"alpha\" labels) (not (member \"beta\" labels))) t (format \"%S\" labels)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "completion prefix: dot, cursor right after the dot offers all members" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 14 :character 6))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"alpha_m\" labels) (member \"beta_m\" labels)) t (format \"%S\" labels)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "completion prefix: dot, cursor in the middle of a member filters by the text before it" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 14 :character 8))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"alpha_m\" labels) (not (member \"beta_m\" labels))) t (format \"%S\" labels)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

fi
