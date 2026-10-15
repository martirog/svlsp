#!/usr/bin/env bash
# test_45_include_context.sh — an `include'd file opened on its own keeps its
# includer's context (plan.md §6.30, "include context"): the server compiles
# it seeded from its include site -- the scope open at the `include, the
# includer's macros -- and a position in it sees the includer's imports and
# the package's members from the other included files. A file included in
# two different scopes gets an Information diagnostic naming the one used.
#
# Fixtures (tests/integration/fixtures):
#   incctx_top.sv   -- `define INCCTX_DECL(n); package incctx_q (incctx_qt);
#                      package incctx_p imports incctx_q::* and includes
#                      incctx_a.svh (class incctx_c) and incctx_b.svh
#   incctx_b.svh    -- class incctx_b extends incctx_c; LSP line 1 declares
#                      incctx_w through `INCCTX_DECL, line 3 is blank, line 5
#                      uses incctx_w
#   incctx_other.sv -- package incctx_r also includes incctx_a.svh

TOP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/incctx_top.sv"
B_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/incctx_b.svh"
A_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/incctx_a.svh"
OTHER_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/incctx_other.sv"

TESTS=(
    "include context: completion in an opened included file offers its includer's import and package"
    "include context: a declaration from the includer's macro resolves in the opened included file"
    "include context: a file included in two scopes gets an Information diagnostic"
)

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in "${TESTS[@]}"; do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "include context (an included file opened on its own)"

# incctx_test <name> <form over `buf` (the opened incctx_b.svh)> <expected>
# Opens the includer first, so its compile records the include sites.
incctx_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((top (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok1 (with-current-buffer top (svlsp-test/wait-for-lsp 15)))
                  (buf (svlsp-test/open-file \"${B_FIXTURE}\"))
                  (ok2 (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (got (when (and ok1 ok2) (with-current-buffer buf (sit-for 1) $2))))
             (svlsp-test/close-file buf)
             (svlsp-test/close-file top)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$3"
}

incctx_test "${TESTS[0]}" \
    "(let* ((result (lsp-request \"textDocument/completion\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position     (list :line 3 :character 2))))
            (items  (cond ((hash-table-p result) (gethash \"items\" result))
                          ((listp result) result)))
            (labels (mapcar (lambda (i) (gethash \"label\" i)) items)))
       (if (and (member \"incctx_qt\" labels) (member \"incctx_c\" labels)
                (member \"incctx_w\" labels))
           t
         (format \"labels: %S\" labels)))" \
    "t"

incctx_test "${TESTS[1]}" \
    "(let* ((result (lsp-request \"textDocument/definition\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position     (list :line 5 :character 4))))
            (loc    (if (hash-table-p result) result (car result))))
       (when loc
         (let ((start (gethash \"start\" (gethash \"range\" loc))))
           (format \"%s:%s\" (file-name-nondirectory (lsp--uri-to-path (gethash \"uri\" loc)))
                   (gethash \"line\" start)))))" \
    "\"incctx_b.svh:1\""

run_test "${TESTS[2]}" \
    "(condition-case err
       (let* ((top   (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok1   (with-current-buffer top (svlsp-test/wait-for-lsp 15)))
              (other (svlsp-test/open-file \"${OTHER_FIXTURE}\"))
              (ok2   (with-current-buffer other (svlsp-test/wait-for-lsp 15))))
         (when ok2 (with-current-buffer other (sit-for 2)))
         (let* ((key   (lsp--fix-path-casing (expand-file-name \"${A_FIXTURE}\")))
                (diags (gethash key (lsp-diagnostics)))
                (info  (seq-find (lambda (d)
                                   (and (eql (gethash \"severity\" d) 3)
                                        (string-match-p \"2 different scopes\"
                                                        (gethash \"message\" d))
                                        (string-match-p \"; using '\"
                                                        (gethash \"message\" d))))
                                 diags)))
           (svlsp-test/close-file other)
           (svlsp-test/close-file top)
           (if (and ok1 ok2 info) t (format \"diags: %S\" diags))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
