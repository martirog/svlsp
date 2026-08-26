#!/usr/bin/env bash
# test_25_completion_fuzzy.sh — verify that textDocument/completion's
# fuzzy/typo-tolerant ranking (session 8, src/lsp/fuzzy_match.h/.cpp) works
# through the real JSON-RPC/LSP layer, not just at the CompletionProvider
# unit-test level.
#
# Fixture: fuzzy_completion.sv — module "sensor" declares WIDTH (parameter),
# report_id and xxrepxx (ports). Four "// probe: ..." comment lines put
# typed-prefix probe text at precise (line, char) positions (see the
# fixture's own header comment for the exact layout/rationale — the probe
# text sits inside a comment specifically so it is never lexed/parsed and
# can't create a spurious symbol of its own):
#   WIDTH   line 27 (0-based), char 19 — exact full-name prefix
#   wdth    line 28 (0-based), char 18 — typo'd/non-contiguous prefix
#   rep     line 29 (0-based), char 17 — ambiguous prefix, two candidates
#   qqqqq   line 30 (0-based), char 19 — prefix matching nothing

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/fuzzy_completion.sv"

section "fuzzy completion matching (textDocument/completion)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "completion matches exact full-name prefix WIDTH (regression baseline)" \
        "completion fuzzy-matches typo'd prefix wdth to WIDTH" \
        "completion fuzzy-match excludes non-subsequence candidates for wdth" \
        "completion returns null for a prefix matching no candidate" \
        "completion ranks contiguous-prefix match report_id above scattered match xxrepxx"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    # --- exact full-name prefix still works (regression baseline) ---------
    run_test "completion matches exact full-name prefix WIDTH (regression baseline)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 27 :character 19))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"WIDTH\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- typo'd/non-contiguous prefix still finds the intended symbol -----
    run_test "completion fuzzy-matches typo'd prefix wdth to WIDTH" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 28 :character 18))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"WIDTH\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- the same typo'd prefix excludes symbols it isn't a subsequence of
    run_test "completion fuzzy-match excludes non-subsequence candidates for wdth" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 28 :character 18))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok
                      (not (member \"report_id\" labels))
                      (not (member \"xxrepxx\" labels)))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- a prefix that is not a subsequence of anything returns null -------
    run_test "completion returns null for a prefix matching no candidate" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 30 :character 19)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- a contiguous/word-start match outranks a scattered/mid-word match
    run_test "completion ranks contiguous-prefix match report_id above scattered match xxrepxx" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 29 :character 17))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items)))
                  (pos-report (cl-position \"report_id\" labels :test #'equal))
                  (pos-xxrep  (cl-position \"xxrepxx\"   labels :test #'equal)))
             (svlsp-test/close-file buf)
             (if (and ok pos-report pos-xxrep (< pos-report pos-xxrep)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
