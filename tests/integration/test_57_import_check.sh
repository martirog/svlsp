#!/usr/bin/env bash
# test_57_import_check.sh — the import check (plan.md §6.37) through
# lsp-mode: in a project (fixtures/import_proj/.svlsp.f), `import p::x;`
# of a name package p doesn't declare gets one Warning over the name, and
# editing it to a name p declares clears it.
#
# Fixture: fixtures/import_proj/imp_use.sv. Positions are 0-based.

IMP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/import_proj/imp_use.sv"

section "import check (plan.md §6.37)"

# imp_test <name> <form over the opened buffer> <expected>
imp_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${IMP_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (got (when ok (with-current-buffer buf $2))))
             (svlsp-test/close-file buf)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$3"
}

# Waits up to SECS for the buffer's diagnostic count to satisfy PRED.
WAIT_DIAGS="(lambda (pred secs)
              (let ((deadline (+ (float-time) secs)))
                (while (and (not (funcall pred (length (lsp--get-buffer-diagnostics))))
                            (< (float-time) deadline))
                  (sit-for 0.1))))"

imp_test "import check: one Warning over the name the package doesn't declare" \
    "(progn
       (funcall ${WAIT_DIAGS} (lambda (n) (>= n 1)) 15)
       (sit-for 0.5)
       (mapconcat
        (lambda (d)
          (let* ((range (lsp:diagnostic-range d))
                 (start (lsp:range-start range))
                 (end   (lsp:range-end range)))
            (format \"%s:%s-%s:%s:%s\" (lsp:position-line start) (lsp:position-character start)
                    (lsp:position-character end) (lsp:diagnostic-severity? d)
                    (lsp:diagnostic-message d))))
        (lsp--get-buffer-diagnostics) \" | \"))" \
    "\"5:22-33:2:'imp_missing' is not declared in package 'imp_api_pkg'\""

imp_test "import check: importing a declared name clears the Warning" \
    "(progn
       (funcall ${WAIT_DIAGS} (lambda (n) (>= n 1)) 15)
       (goto-char (point-min))
       (search-forward \"import imp_api_pkg::imp_missing;\")
       (replace-match \"import imp_api_pkg::*;\")
       (funcall ${WAIT_DIAGS} (lambda (n) (= n 0)) 10)
       (length (lsp--get-buffer-diagnostics)))" \
    "0"
