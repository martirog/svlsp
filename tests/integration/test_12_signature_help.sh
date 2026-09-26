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

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/ref_rename_sighelp.sv"
SYSTASK_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/sighelp_systask.sv"

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
        "system function signature from the static table, variadic tail active"
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
fi
