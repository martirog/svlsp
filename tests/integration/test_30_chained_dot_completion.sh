#!/usr/bin/env bash
# test_30_chained_dot_completion.sh — verify chained/function-call
# dot-completion (plan.md §6.14) through the real JSON-RPC/LSP layer, not
# just at the CompletionProvider unit-test level.
#
# Fixture: chained_dot_completion.sv — Greeter (greet_child) and
# BaseGreeter (greet_base); BaseFactory.get_child() returns BaseGreeter,
# Factory extends BaseFactory and *overrides* get_child() to return
# Greeter instead. "// probe: ..." comment lines put the dot-completion
# trigger text at a precise (line, char) position (see the fixture's own
# header comment).
#
# Position reference (LSP 0-based lines, character right after the chain):
#   line 32, char 33 — "this.get_child().gr"  (inside a Factory method)
#   line 33, char 34 — "super.get_child().gr" (inside a Factory method)
#   line 44, char 41 — "make_factory().get_child().gr" (free functions)
#   line 45, char 31 — "get_num().foo().bar" (broken link: int mid-chain)

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/chained_dot_completion.sv"

section "chained/function-call dot-completion (textDocument/completion)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "three-level chain (two function calls) resolves to greet_child" \
        "this. resolves through Factory's own get_child override" \
        "super. resolves through BaseFactory's get_child, not Factory's override" \
        "super. excludes greet_child (Factory's own override result)" \
        "a broken link (int mid-chain) yields no completions"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "three-level chain (two function calls) resolves to greet_child" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 44 :character 41))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet_child\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "this. resolves through Factory's own get_child override" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 32 :character 33))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet_child\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "super. resolves through BaseFactory's get_child, not Factory's override" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 33 :character 34))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet_base\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "super. excludes greet_child (Factory's own override result)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 33 :character 34))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (not (member \"greet_child\" labels))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "a broken link (int mid-chain) yields no completions" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 45 :character 31)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
