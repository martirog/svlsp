#!/usr/bin/env bash
# test_12_signature_help.sh — verify textDocument/signatureHelp: both
# module/interface/program instantiation port lists (plan.md §6.22), bare
# (undotted) function/task calls (its own follow-up section), and dotted
# calls (`obj.method(`, plan.md §6.27).
#
# Fixture: fixtures/ref_rename_sighelp.sv -- `rrsh_adder` has three ports
# (rrsh_clk, rrsh_rst_n, rrsh_done, in that declaration order); `rrsh_top`
# instantiates it with `.rrsh_clk(rrsh_clk)`, `.rrsh_rst_n(rrsh_rst_n)`
# already connected and the cursor positioned right after `.rrsh_done(`
# (line 25, 0-based, col 15) -- both the positional comma count (this is
# the third argument) and the named-connection lookup ("rrsh_done") should
# agree on activeParameter = 2. `rrsh_compute(input int rrsh_a, input int
# rrsh_b = 4)` and its call site in `rrsh_caller` cover the bare-function-call
# follow-up, including default-value rendering.
#
# The `rrsh_sh_*` declarations further down cover plan.md §6.27's dotted-call
# scoping rules over a real client round trip, each one checking the full
# parameter list (not just non-null/count) so a real function's own
# input/direction/default rendering is actually exercised, not just call
# resolution: `rrsh_sh_obj.rrsh_sh_child_get(` (own-class method, resolved
# via a specific `import rrsh_sh_pkg::rrsh_sh_child;`); `rrsh_sh_obj.
# rrsh_sh_base_get(` (a method inherited through `extends`, two parameters,
# the second with a default value); and `rrsh_sh_holder_obj.rrsh_sh_field.
# rrsh_sh_child_get(` (a two-segment chain through a field declared with its
# full package-qualified type, `rrsh_sh_pkg::rrsh_sh_child`, never imported
# at all -- a different scoping rule than the specific-import case above).
#
# `rrsh_p28_*` covers plan.md §6.28: module port type information in
# signature help. `rrsh_p28_wide(input int rrsh_p28_width, output bit
# rrsh_p28_valid = 1)` -- instantiated (empty arg list) at
# `rrsh_p28_caller` -- checks both a plain typed port and a defaulted one
# render their declared type, not just direction, through a real client
# round trip.
#
# Fixture: fixtures/sighelp_systask.sv -- plan.md §6.29 part C: a
# `$sformatf(` call with the cursor after its second comma resolves from the
# static system-task table (no DB row exists), with activeParameter clamped
# to the variadic `args...` tail.
#
# Fixture: fixtures/sighelp_keywords.sv -- plan.md §6.29 part B: the cursor
# after the second `;` of a `for (` header gets the keyword table's
# `for (initialization; condition; step)` signature with the step active.
#
# Fixture: fixtures/sighelp_macros.sv -- plan.md §6.29 part A: a
# `` `SH29M_LOG( `` call whose macro is defined in an included, `define-only
# header (sighelp_macros.svh), with the cursor on its defaulted third
# parameter.
#
# Fixture: fixtures/sighelp_shorthand.sv -- comma-shorthand parameters
# (`input int shtf_a, shtf_b`): the bare `shtf_b` gets its own slot with
# the inherited type.
#
# Fixture: fixtures/sighelp_ctor.sv -- §6.30 follow-up: `new(` resolves to
# the constructor of the class being constructed (the declared variable's),
# not the enclosing class's.
#
# Fixture: fixtures/sighelp_scoped.sv -- multi-level `::` qualifiers (the
# UVM factory shape): `shsc_item::type_id::shsc_create(` and
# `shsc_pkg::shsc_item::type_id::shsc_create(` resolve through
# shsc_item's own type_id (the object registry), not the enclosing
# shsc_pred's (the component registry); `BUSTYPE::type_id::shsc_create(`
# (a type parameter) gets no signature at all.

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/ref_rename_sighelp.sv"
SYSTASK_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/sighelp_systask.sv"
KEYWORD_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/sighelp_keywords.sv"
MACRO_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/sighelp_macros.sv"
SHORTHAND_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/sighelp_shorthand.sv"
CTOR_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/sighelp_ctor.sv"
SCOPED_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/sighelp_scoped.sv"

# scoped_sig_test <name> <line> <char> <expected label, or "null">
scoped_sig_test() {
    run_test "$1" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SCOPED_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line $2 :character $3))))))
                  (sig    (when result (aref (gethash \"signatures\" result) 0))))
             (svlsp-test/close-file buf)
             (cond ((not ok) \"no-lsp\")
                   ((null result) \"null\")
                   (t (format \"%s @%s\" (gethash \"label\" sig)
                              (gethash \"activeParameter\" sig)))))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "$4"
}

section "signature help (textDocument/signatureHelp)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "null when the cursor isn't inside any parentheses" \
        "returns rrsh_adder's port list with the named-connection active parameter" \
        "returns rrsh_compute's parameter list for a bare function call, default value rendered" \
        "dotted call resolves an own-class method reached via a specific import" \
        "dotted call resolves a method inherited via extends, default value rendered" \
        "two-segment dotted chain resolves through a package-qualified, never-imported field type" \
        "module instantiation port labels carry their declared type, including a default value" \
        "system function signature from the static table, variadic tail active" \
        "for header signature from the keyword table, semicolon-separated" \
        "macro signature from an included define-only header, default rendered" \
        "comma-shorthand parameter gets its own slot with the inherited type" \
        "new( shows the constructed class's constructor, not the enclosing class's" \
        "Class::type_id::method( resolves through that class's type_id" \
        "pkg::Class::type_id::method( resolves through that class's type_id" \
        "TypeParam::type_id::method( gets no signature, not the enclosing class's type_id" \
        "server advertises ( and , as trigger characters and ; as a retrigger"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "null when the cursor isn't inside any parentheses" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 14 :character 0)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "returns rrsh_adder's port list with the named-connection active parameter" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 25 :character 15))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string-prefix-p \"rrsh_adder(\" label)
                      (eql (length params) 3)
                      (eql active 2))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "returns rrsh_compute's parameter list for a bare function call, default value rendered" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 39 :character 37))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"rrsh_compute(int rrsh_a, int rrsh_b = 4)\")
                      (eql (length params) 2)
                      (eql active 1))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "dotted call resolves an own-class method reached via a specific import" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 72 :character 34))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (p0label (when (and params (> (length params) 0))
                             (gethash \"label\" (aref params 0))))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"rrsh_sh_child_get(int rrsh_cx)\")
                      (eql (length params) 1)
                      (string= p0label \"int rrsh_cx\")
                      (eql active 0))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "dotted call resolves a method inherited via extends, default value rendered" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 73 :character 33))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (p0label (when (and params (> (length params) 0))
                             (gethash \"label\" (aref params 0))))
                  (p1label (when (and params (> (length params) 1))
                             (gethash \"label\" (aref params 1))))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"rrsh_sh_base_get(int rrsh_bx, int rrsh_by = 5)\")
                      (eql (length params) 2)
                      (string= p0label \"int rrsh_bx\")
                      (string= p1label \"int rrsh_by = 5\")
                      (eql active 0))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "two-segment dotted chain resolves through a package-qualified, never-imported field type" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 74 :character 55))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (p0label (when (and params (> (length params) 0))
                             (gethash \"label\" (aref params 0))))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"rrsh_sh_child_get(int rrsh_cx)\")
                      (eql (length params) 1)
                      (string= p0label \"int rrsh_cx\")
                      (eql active 0))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "module instantiation port labels carry their declared type, including a default value" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 89 :character 29))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (p0label (when (and params (> (length params) 0))
                             (gethash \"label\" (aref params 0))))
                  (p1label (when (and params (> (length params) 1))
                             (gethash \"label\" (aref params 1))))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"rrsh_p28_wide(input int rrsh_p28_width, output bit rrsh_p28_valid = 1)\")
                      (eql (length params) 2)
                      (string= p0label \"input int rrsh_p28_width\")
                      (string= p1label \"output bit rrsh_p28_valid = 1\")
                      (eql active 0))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "system function signature from the static table, variadic tail active" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SYSTASK_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 10 :character 42))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"\$sformatf(format, args...)\")
                      (eql (length params) 2)
                      (eql active 1))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "for header signature from the keyword table, semicolon-separated" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${KEYWORD_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 6 :character 39))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"for (initialization; condition; step)\")
                      (eql (length params) 3)
                      (eql active 2))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "macro signature from an included define-only header, default rendered" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${MACRO_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 7 :character 33))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"\`SH29M_LOG(ID, MSG, VERB = 1)\")
                      (eql (length params) 3)
                      (eql active 2))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "comma-shorthand parameter gets its own slot with the inherited type" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SHORTHAND_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 8 :character 22))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"shtf_add(int shtf_a, int shtf_b, output bit shtf_c)\")
                      (eql (length params) 3)
                      (eql active 1))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "new( shows the constructed class's constructor, not the enclosing class's" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${CTOR_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/signatureHelp\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 10 :character 24))))))
                  (sig     (when result (aref (gethash \"signatures\" result) 0)))
                  (label   (when sig (gethash \"label\" sig)))
                  (params  (when sig (gethash \"parameters\" sig)))
                  (active  (when sig (gethash \"activeParameter\" sig))))
             (svlsp-test/close-file buf)
             (if (and ok sig
                      (string= label \"new(string shct_name, int shct_n = 0)\")
                      (eql (length params) 2)
                      (eql active 1))
                 t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    OBJ_CREATE='"shsc_create(string shsc_name = \"\", int shsc_parent = 0) @1"'
    # "      x = shsc_item::type_id::shsc_create(\"i\", 1);" (line 21)
    scoped_sig_test "Class::type_id::method( resolves through that class's type_id" \
        21 47 "${OBJ_CREATE}"
    # "      x = shsc_pkg::shsc_item::type_id::shsc_create(\"j\", 2);" (line 22)
    scoped_sig_test "pkg::Class::type_id::method( resolves through that class's type_id" \
        22 57 "${OBJ_CREATE}"
    # "      x = BUSTYPE::type_id::shsc_create(\"t\", 3);" (line 23)
    scoped_sig_test "TypeParam::type_id::method( gets no signature, not the enclosing class's type_id" \
        23 45 '"null"'

    run_test "server advertises ( and , as trigger characters and ; as a retrigger" \
        "(condition-case err
           (let* ((buf  (svlsp-test/open-file \"${SHORTHAND_FIXTURE}\"))
                  (ok   (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (opts (when ok (with-current-buffer buf
                                   (lsp--capability \"signatureHelpProvider\"))))
                  (trig (when (hash-table-p opts) (append (gethash \"triggerCharacters\" opts) nil)))
                  (retr (when (hash-table-p opts) (append (gethash \"retriggerCharacters\" opts) nil))))
             (svlsp-test/close-file buf)
             (if (and ok (equal trig '(\"(\" \",\")) (equal retr '(\";\"))) t
               (format \"trigger=%S retrigger=%S\" trig retr)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
