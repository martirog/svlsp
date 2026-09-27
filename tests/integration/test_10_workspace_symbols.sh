#!/usr/bin/env bash
# test_10_workspace_symbols.sh — verify that workspace/symbol returns real
# results from the DB-backed workspace symbol provider (Phase 6).
#
# After opening module_basic.sv the server parses it, so workspace/symbol
# with query "adder" should return a non-null list containing the module.
# An empty query ("") also returns all symbols (non-null).
# A query that matches nothing ("zzz_no_match") returns null.

SV_FIXTURE="${SVLSP_ROOT}/examples/module_basic.sv"
MACRO_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/workspace_macros.sv"

section "workspace symbols (workspace/symbol)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "server alive after workspace/symbol request"      "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "workspace/symbol returns adder for query 'adder'" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "workspace/symbol returns null for no-match query" "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "workspace/symbol lists a \`define by prefix" "svlsp binary not found at ${SVLSP_BIN}"
else
    # --- server survives a workspace/symbol request -----------------------
    run_test "server alive after workspace/symbol request" \
        "(condition-case err
           (let* ((buf   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok    (with-current-buffer buf
                            (svlsp-test/wait-for-lsp 15)))
                   (alive
                     (when ok
                       (with-current-buffer buf
                         (ignore
                           (lsp-request \"workspace/symbol\"
                                        (list :query \"\"))))
                       (with-current-buffer buf
                         (cl-some (lambda (ws)
                                    (eq (lsp--workspace-status ws) 'initialized))
                                  (lsp-workspaces))))))
             (svlsp-test/close-file buf)
             (if alive t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- query "adder" returns the adder module ---------------------------
    run_test "workspace/symbol returns adder for query 'adder'" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"workspace/symbol\"
                                      (list :query \"adder\"))))))
             (svlsp-test/close-file buf)
             (if (and ok (not (null result))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- query with no match returns null ---------------------------------
    run_test "workspace/symbol returns null for no-match query" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                   (ok     (with-current-buffer buf
                             (svlsp-test/wait-for-lsp 15)))
                   (result
                     (when ok
                       (with-current-buffer buf
                         (lsp-request \"workspace/symbol\"
                                      (list :query \"zzz_no_match\"))))))
             (svlsp-test/close-file buf)
             (if (and ok (null result)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- a `define is listed, as a constant in `define --------------------
    run_test "workspace/symbol lists a \`define by prefix" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${MACRO_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"workspace/symbol\" (list :query \"wsmac_L\")))))
                  (sym    (seq-find (lambda (s) (equal (gethash \"name\" s) \"wsmac_LOG\"))
                                    result))
                  (start  (when sym (gethash \"start\" (gethash \"range\" (gethash \"location\" sym))))))
             (svlsp-test/close-file buf)
             (if (and ok sym
                      (equal (gethash \"containerName\" sym) \"\`define\")
                      (eql (gethash \"kind\" sym) 14)
                      (eql (gethash \"line\" start) 3)
                      (eql (gethash \"character\" start) 8))
                 t (format \"%S\" result)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
