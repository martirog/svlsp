#!/usr/bin/env bash
# test_27_dot_completion.sh — end-to-end proof that dot/member-access
# completion (plan.md §6.10) reaches the real JSON-RPC/LSP layer: triggering
# completion right after `w.` narrows to DotWidget's own members instead of
# every symbol visible in the enclosing scope, `w.spi` further narrows by
# typed prefix, and `plain_int.` (a built-in-typed variable) returns no
# completions rather than falling back to scope-wide completion.
#
# Fixture: tests/integration/fixtures/dot_completion.sv
#   class DotWidget { function spin_up(); int speed_val; }
#   module dot_completion_top; DotWidget w; int plain_int; ... endmodule
#
# The three trigger positions live inside "// probe: ..." comments (see the
# fixture's own header comment for why: bare no-paren text on a real code
# line gets mis-parsed as an implicit-type variable declaration under this
# grammar's data_type ambiguity, self-matching every probe rather than
# testing anything -- same trick fuzzy_completion.sv uses).
#
# Line-number reference (1-based -> LSP 0-based):
#   line 25 -> LSP line 24, char 14 -- "  // probe: w."          (after '.')
#   line 26 -> LSP line 25, char 17 -- "  // probe: w.spi"        (after "spi")
#   line 27 -> LSP line 26, char 22 -- "  // probe: plain_int."   (after '.')

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/dot_completion.sv"
# §6.30 follow-up: a receiver typed by a (chained) class typedef.
# "  // probe: h." -> LSP line 16, char 14.
TYPEDEF_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/typedef_dot_completion.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "w. includes spin_up (DotWidget member)" \
        "w. includes speed_val (DotWidget member)" \
        "w. excludes w itself" \
        "w. excludes dot_completion_top (unrelated scope-wide symbol)" \
        "w.spi includes spin_up (prefix match)" \
        "w.spi excludes speed_val (prefix doesn't match)" \
        "plain_int. (built-in type) returns no completions" \
        "h. through a typedef of a typedef offers the aliased class's members"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "dot/member-access completion (textDocument/completion)"
# ---------------------------------------------------------------------------

run_test "w. includes spin_up (DotWidget member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 24 :character 14))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"spin_up\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "w. includes speed_val (DotWidget member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 24 :character 14))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"speed_val\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "w. excludes w itself" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 24 :character 14))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"w\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "w. excludes dot_completion_top (unrelated scope-wide symbol)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 24 :character 14))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"dot_completion_top\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "w.spi includes spin_up (prefix match)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 25 :character 17))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"spin_up\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "w.spi excludes speed_val (prefix doesn't match)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 25 :character 17))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"speed_val\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "plain_int. (built-in type) returns no completions" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 26 :character 22))))))
              (items  (when (hash-table-p result) (gethash \"items\" result))))
         (svlsp-test/close-file buf)
         (if (and ok (or (null result) (null items))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "h. through a typedef of a typedef offers the aliased class's members" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TYPEDEF_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 16 :character 14))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"tdd_fld\" labels) (member \"tdd_run\" labels)
                  (not (member \"tdd_top\" labels)))
             t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
