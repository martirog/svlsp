#!/usr/bin/env bash
# test_51_type_parameters.sh — type parameters (plan.md §6.21 step 1):
# definition on a type parameter's use and on a `#(.T(...))` connection
# lands on the parameter, hover names it with its default type, and a value
# of its type resolves through the default -- definition on a method lands
# on the default class's, never tprm_decoy's, and dot completion offers the
# default class's members.
#
# Fixture: fixtures/typeparam.sv. Positions are 0-based (line, character).

TYPEPARAM_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/typeparam.sv"

section "type parameters"

# tprm_test <name> <form over the opened buffer> <expected>
tprm_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${TYPEPARAM_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (got (when ok (with-current-buffer buf $2))))
             (svlsp-test/close-file buf)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$3"
}

# def_test <name> <line> <char> <expected-line> <expected-char>
def_test() {
    tprm_test "$1" \
        "(let* ((result (lsp-request \"textDocument/definition\"
                                     (list :textDocument (list :uri (lsp--buffer-uri))
                                           :position     (list :line $2 :character $3))))
                (loc    (if (hash-table-p result) result (car result)))
                (start  (when loc (gethash \"start\" (gethash \"range\" loc)))))
           (when start (format \"%s:%s\" (gethash \"line\" start) (gethash \"character\" start))))" \
        "\"$4:$5\""
}

def_test "type parameters: definition on a use of T lands on the parameter" \
    11 4 10 24
def_test "type parameters: definition on #(.T(...)) lands on the parameter" \
    23 14 10 24
def_test "type parameters: definition through a T-typed value lands on the default class's method" \
    14 19 8 17

tprm_test "type parameters: hover names the parameter and its default type" \
    "(let* ((result (lsp-request \"textDocument/hover\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position     (list :line 11 :character 4))))
            (value  (gethash \"value\" (gethash \"contents\" result))))
       (if (and (string-match-p \"TypeParameter\\\\*\\\\* \`T\`\" value)
                (string-match-p \"tprm_item\" value))
           t
         value))" \
    "t"

tprm_test "type parameters: completion after a T-typed value offers the default class's members" \
    "(let* ((result (lsp-request \"textDocument/completion\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position     (list :line 16 :character 13))))
            (items  (cond ((hash-table-p result) (gethash \"items\" result))
                          ((listp result) result)))
            (labels (mapcar (lambda (i) (gethash \"label\" i)) items)))
       (if (and (member \"tprm_field\" labels) (member \"tprm_get\" labels)
                (not (member \"tprm_v\" labels)))
           t
         (format \"labels: %S\" labels)))" \
    "t"
