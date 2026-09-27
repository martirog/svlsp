#!/usr/bin/env bash
# test_29_builtin_method_completion.sh — verify built-in container/type
# method completion (plan.md §6.13) through the real JSON-RPC/LSP layer,
# not just at the CompletionProvider unit-test level.
#
# Fixture: builtin_method_completion.sv — a queue, an associative array, a
# mailbox, a process, and a plain class instance with no explicitly-declared
# randomize. "probe: ..." lines put the dot-completion trigger text at a
# precise (line, char) position (see the fixture's own header comment for
# why the probe text sits in a never-defined `ifdef).
#
# Position reference (LSP 0-based lines, character right after the '.'):
#   line 25, char 14 — "q."
#   line 26, char 15 — "aa."
#   line 27, char 16 — "mbx."
#   line 28, char 14 — "p."
#   line 29, char 16 — "obj."

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/builtin_method_completion.sv"

section "built-in container/type method completion (textDocument/completion)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "queue completion includes push_back" \
        "queue completion excludes exists (associative-array-only)" \
        "associative array completion includes exists" \
        "associative array completion excludes push_back (queue-only)" \
        "mailbox completion includes put and get" \
        "mailbox completion excludes status (process-only)" \
        "process completion includes status and self" \
        "class instance completion includes implicit randomize" \
        "class instance completion includes its own declared member"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "queue completion includes push_back" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 25 :character 14))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"push_back\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "queue completion excludes exists (associative-array-only)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 25 :character 14))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (not (member \"exists\" labels))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "associative array completion includes exists" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 26 :character 15))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"exists\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "associative array completion excludes push_back (queue-only)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 26 :character 15))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (not (member \"push_back\" labels))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "mailbox completion includes put and get" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 27 :character 16))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"put\" labels) (member \"get\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "mailbox completion excludes status (process-only)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 27 :character 16))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (not (member \"status\" labels))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "process completion includes status and self" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 28 :character 14))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"status\" labels) (member \"self\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "class instance completion includes implicit randomize" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 29 :character 16))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"randomize\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "class instance completion includes its own declared member" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 29 :character 16))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
