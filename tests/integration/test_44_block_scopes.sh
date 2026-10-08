#!/usr/bin/env bash
# test_44_block_scopes.sh — block scopes through the real client (plan.md
# §6.30, "block scopes"): same-named locals in sibling begin/end blocks and
# same-named for-loop variables are distinct symbols for definition,
# references, rename and completion. Complements the [block] unit cases.
#
# Fixture: tests/integration/fixtures/block_scopes.sv (LSP lines):
#   5/10   -- `int bsc_x;` in two sibling initial blocks
#   12     -- an inner block's `int bsc_x;` shadowing line 10's; used at 15
#   18, 20 -- `for (int bsc_i = ...)` twice in bsc_f
#   22     -- a blank line inside the second loop's body

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/block_scopes.sv"

TESTS=(
    "block scopes: definition after an inner block skips the inner block's shadowing local"
    "block scopes: references of a block local skip the sibling block's"
    "block scopes: references of a loop variable stay in its loop"
    "block scopes: rename of a loop variable edits only its loop"
    "block scopes: completion in a loop body offers its variable, not another block's"
)

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in "${TESTS[@]}"; do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "block scopes (definition, references, rename, completion)"

# loc_fmt: "basename:line:char" of an LSP location's start.
LOC_FMT="(lambda (uri range)
           (let ((start (gethash \"start\" range)))
             (format \"%s:%s:%s\" (file-name-nondirectory (lsp--uri-to-path uri))
                     (gethash \"line\" start) (gethash \"character\" start))))"

# bsc_test <name> <method> <line> <char> <extra-params> <result->string form over `result`> <expected>
bsc_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((fmt    ${LOC_FMT})
                  (buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"$2\"
                                           (append (list :textDocument (list :uri (lsp--buffer-uri))
                                                         :position     (list :line $3 :character $4))
                                                   $5)))))
                  (got    $6))
             (svlsp-test/close-file buf)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$7"
}

LOCS_FORM="(mapconcat #'identity
             (sort (mapcar (lambda (loc) (funcall fmt (gethash \"uri\" loc) (gethash \"range\" loc)))
                           result)
                   #'string<)
             \" \")"

bsc_test "${TESTS[0]}" "textDocument/definition" 15 4 "nil" \
    "(let ((loc (if (hash-table-p result) result (car result))))
       (when loc (funcall fmt (gethash \"uri\" loc) (gethash \"range\" loc))))" \
    "\"block_scopes.sv:10:8\""

bsc_test "${TESTS[1]}" "textDocument/references" 5 8 \
    "(list :context (list :includeDeclaration t))" "${LOCS_FORM}" \
    "\"block_scopes.sv:5:8 block_scopes.sv:6:4 block_scopes.sv:7:12\""

bsc_test "${TESTS[2]}" "textDocument/references" 21 15 \
    "(list :context (list :includeDeclaration t))" "${LOCS_FORM}" \
    "\"block_scopes.sv:20:13 block_scopes.sv:20:24 block_scopes.sv:20:35 block_scopes.sv:21:15\""

bsc_test "${TESTS[3]}" "textDocument/rename" 19 15 "(list :newName \"bsc_j\")" \
    "(let (got)
       (when result
         (maphash (lambda (uri edits)
                    (seq-doseq (te edits) (push (funcall fmt uri (gethash \"range\" te)) got)))
                  (gethash \"changes\" result)))
       (mapconcat #'identity (sort got #'string<) \" \"))" \
    "\"block_scopes.sv:18:13 block_scopes.sv:18:24 block_scopes.sv:18:35 block_scopes.sv:19:15\""

bsc_test "${TESTS[4]}" "textDocument/completion" 22 0 "nil" \
    "(let* ((items  (cond ((hash-table-p result) (gethash \"items\" result))
                          ((listp result) result)))
            (labels (mapcar (lambda (i) (gethash \"label\" i)) items)))
       (if (and (member \"bsc_i\" labels) (member \"bsc_r\" labels)
                (not (member \"bsc_x\" labels))
                (not (seq-some (lambda (l) (string-prefix-p \"@\" l)) labels)))
           t
         (format \"labels: %S\" labels)))" \
    "t"
