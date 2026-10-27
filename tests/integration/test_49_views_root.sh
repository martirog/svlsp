#!/usr/bin/env bash
# test_49_views_root.sh — modports and clocking blocks as views of their
# interface, and $root paths (plan.md §6.30 leftovers): definition through
# a clocking block and a modport lands on the interface's own signal, hover
# names the clocking block, completion after a modport-typed handle offers
# only what the modport lists, and $root. starts at the top-level design
# units -- never the same-named decoys in vwr_decoy.
#
# Fixture: fixtures/views_root.sv. Positions are 0-based (line, character).

VIEWS_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/views_root.sv"

section "modports, clocking blocks and \$root"

# views_test <name> <form over the opened buffer> <expected>
views_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${VIEWS_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (got (when ok (with-current-buffer buf $2))))
             (svlsp-test/close-file buf)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$3"
}

# def_test <name> <line> <char> <expected-line> <expected-char>
def_test() {
    views_test "$1" \
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
    views_test "$1" \
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

def_test "views: definition through a clocking block lands on the interface signal" \
    27 20 8 14
def_test "views: definition through a modport-typed handle lands on the interface signal" \
    28 9 9 8
def_test "\$root: definition on the unit after \$root lands on the module" \
    29 15 21 7
def_test "\$root: definition at the end of a \$root path lands on the signal" \
    29 29 18 8

views_test "views: hover on a clocking block names it and lists its clockvars" \
    "(let* ((result (lsp-request \"textDocument/hover\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position     (list :line 27 :character 13))))
            (value  (gethash \"value\" (gethash \"contents\" result))))
       (and (string-match-p \"Clocking\\\\*\\\\* \`vwr_cb\`\" value)
            (string-match-p \"input vwr_data\" value)
            t))" \
    "t"

completion_test "views: completion after a modport-typed handle offers only the modport's members" \
    32 17 "\"vwr_data\" \"vwr_valid\" \"vwr_drive\" \"vwr_cb\"" "\"vwr_only\""
completion_test "\$root: completion after \$root. offers the top-level design units" \
    33 18 "\"vwr_top\" \"vwr_sub\" \"vwr_if\"" "\"vif\" \"x\" \"vwr_sig\""
