#!/usr/bin/env bash
# test_12_signature_help.sh — verify that svlsp handles
# textDocument/signatureHelp without crashing and returns a null result
# (pre-ANTLR4, no port-list or function-signature information yet).
#
# Phase 3.10: the signature-help handler is wired and routing works, but the
# server has no parser or database, so it always responds with JSON null.
# Phase 4 will return SignatureHelp with port/parameter signatures when the
# cursor is inside a module instantiation or function call.
#
# lsp-request sends a synchronous textDocument/signatureHelp RPC.  JSON null
# maps to Emacs nil, so a null response is verified with (null result).

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"

section "signature help (textDocument/signatureHelp)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after signatureHelp request" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "signatureHelp returns null (pre-ANTLR4)"  "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a signatureHelp request --------------------------
    run_test "server alive after signatureHelp request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"textDocument/signatureHelp\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 0 :character 0)))))
                       (with-current-buffer buf
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- null result (no parser yet) --------------------------------------
    run_test "signatureHelp returns null (pre-ANTLR4)" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"textDocument/signatureHelp\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     (list :line 0 :character 5)))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
