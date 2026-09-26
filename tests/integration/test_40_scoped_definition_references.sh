#!/usr/bin/env bash
# test_40_scoped_definition_references.sh — real-client round trips for
# go-to-definition through `::`, `include and `.`, and exact-location
# find-references per declaration kind.
#
# Complements the unit suites tests/unit/lsp/test_definition_scoped.cpp and
# test_references_scoped.cpp (and test_rename.cpp's [scoped] cases). The
# fixture's second half (plan.md §6.30 step B) redeclares every name in a
# second package/module, so every case here -- including the original
# references ones -- also checks that a same-named decoy is excluded.
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

# rename_test <name> <line> <char> <new-name> <expected edit starts, as refs_test>
# Collects the edit positions without applying them (the fixture buffer is
# shared with every other test here).
rename_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((lsp-response-timeout 60)
                  (buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (edit   (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/rename\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line $2 :character $3)
                                                 :newName      \"$4\")))))
                  (got    nil))
             (when edit
               (maphash (lambda (uri edits)
                          (seq-doseq (te edits)
                            (let ((start (gethash \"start\" (gethash \"range\" te))))
                              (push (format \"%s:%s:%s\"
                                            (file-name-nondirectory (lsp--uri-to-path uri))
                                            (gethash \"line\" start)
                                            (gethash \"character\" start))
                                    got))))
                        (gethash \"changes\" edit)))
             (svlsp-test/close-file buf)
             (mapconcat #'identity (sort got #'string<) \" \"))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "\"$5\""
}

DEF_TESTS=(
    "definition: package half of pkg::Class lands on the package"
    "definition: class half of pkg::Class lands on the class"
    "definition: class declared in an included file lands in that file"
    "definition: dotted call into an included class lands in the included file"
    "definition: dotted method call lands on the receiver class's method"
    "definition: dotted field access lands on the receiver class's field"
    "definition: pkg_b::Class lands on pkg_b's class, not pkg_a's"
    "definition: dotted call on a pkg_b receiver lands on pkg_b's method"
    "definition: dotted field on a pkg_b receiver lands on pkg_b's field"
    "definition: a signal lands on its own module's declaration"
    "definition: a typedef use lands on the typedef"
    "definition: an enum literal use lands on the literal"
    "definition: a field used in an out-of-class body lands on the class's field"
)
REF_TESTS=(
    "references: signal -- declaration + both named-connection uses"
    "references: module -- declaration + both instantiations"
    "references: port -- declaration + both named connections"
    "references: class from an included file -- both files"
    "references: class field -- declaration, own-method use, dotted use"
    "references: pkg_b's field -- excludes pkg_a's same-named field"
    "references: second module's signal -- excludes the first module's"
    "references: typedef -- declaration and use"
    "references: enum literal -- declaration and use"
    "references: class field -- includes the out-of-class body's use, not the package var"
)
RENAME_TESTS=(
    "rename: edits only the cursor module's signal, not a same-named one"
)

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in "${DEF_TESTS[@]}" "${REF_TESTS[@]}" "${RENAME_TESTS[@]}"; do
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
    # "  defref_pkg_b::defref_Item b;" (line 35) -> pkg_b's class (line 29)
    def_test "${DEF_TESTS[6]}" 35 16 defref_top.sv 29 8
    # "    r = b.defref_get();" (line 39)
    def_test "${DEF_TESTS[7]}" 39 10 defref_top.sv 31 17
    # "    b.defref_val = defref_sig;" (line 40)
    def_test "${DEF_TESTS[8]}" 40 6  defref_top.sv 30 8
    def_test "${DEF_TESTS[9]}" 40 19 defref_top.sv 36 8
    # defref_kinds (plan.md §6.30 step C): "  defref_nib_t n;" (line 47) and
    # "  initial defref_st = DEFREF_ON;" (line 48)
    def_test "${DEF_TESTS[10]}" 47 2  defref_top.sv 45 22
    def_test "${DEF_TESTS[11]}" 48 22 defref_top.sv 46 20
    # defref_ooc_p (plan.md §6.30 step D): "    defref_cnt = 1;" (line 58) in
    # defref_Ooc::defref_run's out-of-class body; the package-level decoy
    # defref_cnt is on line 52, the class field on line 54.
    def_test "${DEF_TESTS[12]}" 58 4  defref_top.sv 54 8

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
    # "    int defref_val;" in defref_pkg_b (line 30)
    refs_test "${REF_TESTS[5]}" 30 8 \
        "defref_top.sv:30:8 defref_top.sv:31:38 defref_top.sv:40:6"
    # "  logic defref_sig;" in defref_top_b (line 36)
    refs_test "${REF_TESTS[6]}" 36 8 \
        "defref_top.sv:36:8 defref_top.sv:40:19"
    refs_test "${REF_TESTS[7]}" 45 22 \
        "defref_top.sv:45:22 defref_top.sv:47:2"
    refs_test "${REF_TESTS[8]}" 46 20 \
        "defref_top.sv:46:20 defref_top.sv:48:22"
    refs_test "${REF_TESTS[9]}" 54 8 \
        "defref_top.sv:54:8 defref_top.sv:58:4"

    rename_test "${RENAME_TESTS[0]}" 36 8 defref_sig_b \
        "defref_top.sv:36:8 defref_top.sv:40:19"
fi
