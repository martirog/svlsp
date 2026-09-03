#!/usr/bin/env bash
# test_34_package_scoped_dot_completion.sh — verify dot-completion resolves
# into a class declared inside a package (plan.md §6.17) through the real
# JSON-RPC/LSP layer, not just at the CompletionProvider unit-test level.
#
# Fixture: package_scoped_dot_completion.sv — PolicyBase extends an
# unresolved external base (uvm_object) and is declared *inside a
# package*, directly re-creating the real motivating bug shape. Before
# this fix, dot-completion on a PolicyBase-typed variable only ever
# offered the synthetic randomize-family methods, never PolicyBase's own
# real, declared get_policy().
#
# Positions are found via search-forward on literal fixture text, not
# hand-counted line/char constants — see test_32_live_edit_completion.sh's
# own header comment for why.

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/package_scoped_dot_completion.sv"

section "dot-completion into a package-nested class's members (plan.md §6.17)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "dot-completion on a package-nested class offers its real declared method" \
        "dot-completion on a package-nested class still offers the implicit randomize methods too"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "dot-completion on a package-nested class offers its real declared method" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result
                   (when ok
                     (with-current-buffer buf
                       (goto-char (point-min))
                       (search-forward \"policy.get\")
                       (lsp-request \"textDocument/completion\"
                                    (list :textDocument (list :uri (lsp--buffer-uri))
                                          :position     (list :line (1- (line-number-at-pos))
                                                               :character (- (point) (line-beginning-position))))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"get_policy\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "dot-completion on a package-nested class still offers the implicit randomize methods too" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result
                   (when ok
                     (with-current-buffer buf
                       (goto-char (point-min))
                       (search-forward \"policy.get\")
                       (lsp-request \"textDocument/completion\"
                                    (list :textDocument (list :uri (lsp--buffer-uri))
                                          :position     (list :line (1- (line-number-at-pos))
                                                               :character (- (point) (line-beginning-position))))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"get_randstate\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
