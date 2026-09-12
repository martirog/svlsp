#!/usr/bin/env bash
# test_12_signature_help.sh — verify textDocument/signatureHelp (plan.md
# item 3), scoped to module/interface/program instantiation port lists --
# see SignatureHelpProvider's own doc comment for why function/task calls
# aren't supported.
#
# Fixture: fixtures/ref_rename_sighelp.sv -- `rrsh_adder` has three ports
# (rrsh_clk, rrsh_rst_n, rrsh_done, in that declaration order); `rrsh_top`
# instantiates it with `.rrsh_clk(rrsh_clk)`, `.rrsh_rst_n(rrsh_rst_n)`
# already connected and the cursor positioned right after `.rrsh_done(`
# (line 25, 0-based, col 15) -- both the positional comma count (this is
# the third argument) and the named-connection lookup ("rrsh_done") should
# agree on activeParameter = 2.

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/ref_rename_sighelp.sv"

section "signature help (textDocument/signatureHelp)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "null when the cursor isn't inside any parentheses" \
        "returns rrsh_adder's port list with the named-connection active parameter"
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
fi
