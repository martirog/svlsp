#!/usr/bin/env bash
# test_31_queue_element_completion.sh — verify queue/associative-array
# element access completion (plan.md §6.15) through the real JSON-RPC/LSP
# layer, not just at the CompletionProvider unit-test level.
#
# Fixture: queue_element_completion.sv — Widget (greet) is the class-typed
# element throughout; `arr` is a fixed array of queues of Widget so both
# partial indexing (arr[i], still a queue) and full indexing (arr[i][j],
# reaches the Widget element) can be distinguished; `iq` is a plain int
# queue, a fail-closed regression guard. "// probe: ..." comment lines put
# the dot-completion trigger text at a precise (line, char) position (see
# the fixture's own header comment).
#
# Position reference (LSP 0-based lines, character right after the '.'):
#   line 27, char 17 — "q[0]."
#   line 28, char 20 — "aa[\"k\"]."
#   line 29, char 19 — "arr[i]."
#   line 30, char 22 — "arr[i][j]."
#   line 31, char 18 — "iq[0]."

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/queue_element_completion.sv"

section "queue/associative-array element access completion (textDocument/completion)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "indexing a queue of a class resolves to the class's own members" \
        "indexing an associative array of a class resolves to the class's own members" \
        "partially indexing a fixed-array-of-queues offers the queue's own methods" \
        "partially indexing a fixed-array-of-queues excludes the class's own members" \
        "fully indexing a fixed-array-of-queues offers the class's own members" \
        "fully indexing a fixed-array-of-queues excludes queue methods" \
        "indexing a queue of a built-in type yields no completions"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "indexing a queue of a class resolves to the class's own members" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 27 :character 17))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "indexing an associative array of a class resolves to the class's own members" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 28 :character 20))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "partially indexing a fixed-array-of-queues offers the queue's own methods" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 29 :character 19))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"push_back\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "partially indexing a fixed-array-of-queues excludes the class's own members" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 29 :character 19))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (not (member \"greet\" labels))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "fully indexing a fixed-array-of-queues offers the class's own members" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 30 :character 22))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "fully indexing a fixed-array-of-queues excludes queue methods" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 30 :character 22))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (not (member \"push_back\" labels))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "indexing a queue of a built-in type yields no completions" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 31 :character 18)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
