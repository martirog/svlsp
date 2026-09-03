#!/usr/bin/env bash
# test_33_prototype_methods.sh — verify function/task-prototype recording
# (pure virtual, extern, interface-class methods; plan.md §6.16) through the
# real JSON-RPC/LSP layer, not just at the tree-walker/completion-provider
# unit-test level.
#
# Fixture: prototype_methods.sv — Policy is an interface class (always
# prototype-only) with a pure-virtual describe(); PolicyImpl is a regular
# class extending an unresolved external base (uvm_object, never declared
# in this project, modeling the real UVM shape that motivated this fix)
# with its own pure-virtual get_policy(); all_policies is a queue of
# PolicyImpl, reusing §6.15's indexed dot-completion. Before this fix,
# neither method would ever appear anywhere (hover, completion,
# documentSymbol) — both are prototype-only, so the tree-walker never
# recorded them as symbols at all, and the file wouldn't even parse
# cleanly (interface_class_declaration was dead grammar).
#
# Positions are found via search-forward on literal fixture text, not
# hand-counted line/char constants — see test_32_live_edit_completion.sh's
# own header comment for why.

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/prototype_methods.sv"

section "function/task prototype recording — pure virtual/extern/interface-class methods (plan.md §6.16)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "the file parses with zero diagnostics (interface class is no longer dead grammar)" \
        "indexing a queue of a class whose only method is pure-virtual offers that method" \
        "hover on a pure-virtual method's own declaration shows its return type"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "the file parses with zero diagnostics (interface class is no longer dead grammar)" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (n   (when ok (with-current-buffer buf
                                  (sit-for 2)
                                  (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             (if (and ok (eql n 0)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "indexing a queue of a class whose only method is pure-virtual offers that method" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result
                   (when ok
                     (with-current-buffer buf
                       (goto-char (point-min))
                       (search-forward \"all_policies[0].get\")
                       (let ((pos (list :line (1- (line-number-at-pos))
                                        :character (- (point) (line-beginning-position)))))
                         (lsp-request \"textDocument/completion\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     pos))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"get_policy\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "hover on a pure-virtual method's own declaration shows its return type" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result
                   (when ok
                     (with-current-buffer buf
                       (goto-char (point-min))
                       (search-forward \"PolicyImpl get_policy\")
                       (backward-char 3)
                       (lsp-request \"textDocument/hover\"
                                    (list :textDocument (list :uri (lsp--buffer-uri))
                                          :position     (list :line (1- (line-number-at-pos))
                                                               :character (- (point) (line-beginning-position))))))))
                  (contents (when (hash-table-p result) (gethash \"contents\" result)))
                  (value    (when (hash-table-p contents) (gethash \"value\" contents))))
             (svlsp-test/close-file buf)
             (if (and ok (stringp value)
                      (string-match-p \"get_policy\" value)
                      (string-match-p \"PolicyImpl\" value))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
