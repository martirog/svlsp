#!/usr/bin/env bash
# test_40_scoped_definition_references.sh — real-client round trips for
# go-to-definition through `::`, `include and `.`, and exact-location
# find-references per declaration kind.
#
# Complements the unit suites tests/unit/lsp/test_definition_scoped.cpp and
# test_references_scoped.cpp. Only cases that resolve correctly today are
# exercised here (the suite must stay green); the scope-strict cases the
# name-only resolution still gets wrong live in those unit files, tagged
# [!shouldfail].
#
# Fixtures: fixtures/defref_top.sv `include`s fixtures/defref_inc.svh.
# Positions below are 0-based (line, character), as sent/returned over LSP.
# Every assertion checks the exact file, line and column -- never only
# non-null or a count.
#
# lsp-response-timeout is raised to 60s per request: the first didOpen of
# this fixture in a fresh daemon costs ~12s of cold ANTLR parsing on the
# ASan debug build, past lsp-mode's 10s default, so the first request
# timed out when this file was run on its own.

TOP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/defref_top.sv"

section "scoped go-to-definition and find-references"

# def_test <name> <line> <char> <expected-basename> <expected-line> <expected-char>
def_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/definition\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line $2 :character $3))))))
                  (loc    (if (hash-table-p result) result (car result)))
                  (start  (when loc (gethash \"start\" (gethash \"range\" loc))))
                  (got    (when loc
                            (format \"%s:%s:%s\"
                                    (file-name-nondirectory (lsp--uri-to-path (gethash \"uri\" loc)))
                                    (gethash \"line\" start)
                                    (gethash \"character\" start)))))
             (svlsp-test/close-file buf)
             got)
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "\"$4:$5:$6\""
}

# refs_test <name> <line> <char> <expected, space-separated sorted basename:line:char>
refs_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/references\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line $2 :character $3)
                                                 :context      (list :includeDeclaration t))))))
                  (got    (mapcar (lambda (loc)
                                    (let ((start (gethash \"start\" (gethash \"range\" loc))))
                                      (format \"%s:%s:%s\"
                                              (file-name-nondirectory
                                               (lsp--uri-to-path (gethash \"uri\" loc)))
                                              (gethash \"line\" start)
                                              (gethash \"character\" start))))
                                  result)))
             (svlsp-test/close-file buf)
             (mapconcat #'identity (sort got #'string<) \" \"))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "\"$4\""
}

DEF_TESTS=(
    "definition: package half of pkg::Class lands on the package"
    "definition: class half of pkg::Class lands on the class"
    "definition: class declared in an included file lands in that file"
    "definition: dotted call into an included class lands in the included file"
    "definition: dotted method call lands on the receiver class's method"
    "definition: dotted field access lands on the receiver class's field"
)
REF_TESTS=(
    "references: signal -- declaration + both named-connection uses"
    "references: module -- declaration + both instantiations"
    "references: port -- declaration + both named connections"
    "references: class from an included file -- both files"
    "references: class field -- declaration, own-method use, dotted use"
)

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in "${DEF_TESTS[@]}" "${REF_TESTS[@]}"; do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    # "  defref_pkg_a::defref_Item a;" (line 14)
    def_test "${DEF_TESTS[0]}" 14 2  defref_top.sv 5 8
    def_test "${DEF_TESTS[1]}" 14 16 defref_top.sv 6 8
    # "  defref_IncCls inc;" (line 15) -> defref_inc.svh "class defref_IncCls;" (line 2)
    def_test "${DEF_TESTS[2]}" 15 2  defref_inc.svh 2 6
    # "    inc.defref_hello();" (line 23) -> defref_inc.svh line 3
    def_test "${DEF_TESTS[3]}" 23 8  defref_inc.svh 3 16
    # "    r = a.defref_get();" (line 21)
    def_test "${DEF_TESTS[4]}" 21 10 defref_top.sv 8 17
    # "    r = a.defref_val;" (line 22)
    def_test "${DEF_TESTS[5]}" 22 10 defref_top.sv 7 8

    # "  logic defref_sig;" (line 16)
    refs_test "${REF_TESTS[0]}" 16 8 \
        "defref_top.sv:16:8 defref_top.sv:18:29 defref_top.sv:19:29"
    # "module defref_leaf(...)" (line 11)
    refs_test "${REF_TESTS[1]}" 11 7 \
        "defref_top.sv:11:7 defref_top.sv:18:2 defref_top.sv:19:2"
    refs_test "${REF_TESTS[2]}" 11 31 \
        "defref_top.sv:11:31 defref_top.sv:18:18 defref_top.sv:19:18"
    refs_test "${REF_TESTS[3]}" 15 2 \
        "defref_inc.svh:2:6 defref_top.sv:15:2"
    refs_test "${REF_TESTS[4]}" 7 8 \
        "defref_top.sv:22:10 defref_top.sv:7:8 defref_top.sv:8:38"
fi
