#!/usr/bin/env bash
# test_47_receivers.sh — struct, interface and hierarchical receivers
# (plan.md §6.30, "Follow-up: struct, interface and hierarchical
# receivers"): definition, references and dot completion through a struct
# variable, a virtual interface, an interface port, a module instance and a
# named block land on that receiver's own member, never on the same-named
# decoy in rcvr_decoy.
#
# Fixture: fixtures/rcvr_top.sv. Positions are 0-based (line, character).

TOP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/rcvr_top.sv"

section "struct, interface and hierarchical receivers"

# rcvr_test <name> <form over the opened buffer> <expected>
rcvr_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (got (when ok (with-current-buffer buf $2))))
             (svlsp-test/close-file buf)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$3"
}

# def_test <name> <line> <char> <expected-line> <expected-char>
def_test() {
    rcvr_test "$1" \
        "(let* ((result (lsp-request \"textDocument/definition\"
                                     (list :textDocument (list :uri (lsp--buffer-uri))
                                           :position     (list :line $2 :character $3))))
                (loc    (if (hash-table-p result) result (car result)))
                (start  (when loc (gethash \"start\" (gethash \"range\" loc)))))
           (when start (format \"%s:%s\" (gethash \"line\" start) (gethash \"character\" start))))" \
        "\"$4:$5\""
}

# completion_test <name> <line> <char> <labels that must be present> <labels that must not>
completion_test() {
    rcvr_test "$1" \
        "(let* ((result (lsp-request \"textDocument/completion\"
                                     (list :textDocument (list :uri (lsp--buffer-uri))
                                           :position     (list :line $2 :character $3))))
                (items  (cond ((hash-table-p result) (gethash \"items\" result))
                              ((listp result) result)))
                (labels (mapcar (lambda (i) (gethash \"label\" i)) items)))
           (if (and (seq-every-p (lambda (l) (member l labels)) '($4))
                    (not (seq-some (lambda (l) (member l labels)) '($5))))
               t
             (format \"labels: %S\" labels)))" \
        "t"
}

def_test "receivers: definition through a struct variable lands on the struct's member" \
    32 6 11 23
def_test "receivers: definition through a virtual interface lands on the interface signal" \
    33 8 15 8
def_test "receivers: definition through an interface port lands on the interface signal" \
    34 14 15 8
def_test "receivers: definition through a module instance lands on the module's signal" \
    35 11 20 8
def_test "receivers: definition on an instance name lands on the instance" \
    35 4 29 11
def_test "receivers: definition through a named block lands on the block's local" \
    36 13 31 8

rcvr_test "receivers: references on a struct member are its declaration and its one use" \
    "(let* ((result (lsp-request \"textDocument/references\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position     (list :line 11 :character 23)
                                       :context      (list :includeDeclaration t))))
            (got    (mapcar (lambda (loc)
                              (let ((start (gethash \"start\" (gethash \"range\" loc))))
                                (format \"%s:%s\" (gethash \"line\" start)
                                        (gethash \"character\" start))))
                            result)))
       (mapconcat #'identity (sort got #'string<) \" \"))" \
    "\"11:23 32:6\""

completion_test "receivers: completion after a virtual interface offers the interface's members" \
    39 16 "\"rcvr_valid\" \"rcvr_drive\" \"clk\"" "\"rcvr_hi\" \"rcvr_sig\""
completion_test "receivers: completion after a struct variable offers the struct's fields" \
    40 14 "\"rcvr_hi\" \"rcvr_lo\"" "\"rcvr_valid\" \"rcvr_sig\""
completion_test "receivers: completion after a module instance offers the module's members" \
    41 19 "\"rcvr_sig\" \"a\"" "\"rcvr_valid\" \"rcvr_hi\""
completion_test "receivers: completion after a module instance offers its named blocks" \
    57 22 "\"rcvr_gen\"" "\"rcvr_anon\" \"rcvr_w\""
completion_test "receivers: completion after an instance's named block offers the block's members" \
    58 31 "\"rcvr_w\"" "\"rcvr_gen\""
