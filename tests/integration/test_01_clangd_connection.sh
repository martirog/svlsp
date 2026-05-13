#!/usr/bin/env bash
# test_01_clangd_connection.sh — validate the test harness by connecting to
# clangd (a known-good LSP server) with a C++ fixture file.
#
# This test must pass before any svlsp tests are written. If it fails, the
# problem is in the test infrastructure, not in svlsp.

CPP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/hello.cpp"

section "clangd connection (harness validation)"

# Open the C++ file, activate c++-mode, connect to clangd, wait for init.
run_test "clangd connects and initializes for a C++ file" \
    "(let* ((buf (svlsp-test/open-file \"${CPP_FIXTURE}\"))
             (ok  (with-current-buffer buf
                    (svlsp-test/wait-for-lsp 30))))
       (svlsp-test/close-file buf)
       (if ok t nil))" \
    "t"

section "clangd hover"

# Re-open the file, move to a known symbol, and request hover text.
run_test "hover returns non-empty result on a known identifier" \
    "(let* ((buf (svlsp-test/open-file \"${CPP_FIXTURE}\"))
             (result
              (with-current-buffer buf
                (svlsp-test/wait-for-lsp 30)
                ;; Position point on 'greet' (line 6, char 13 in 0-based LSP coords).
                (goto-char (point-min))
                (forward-line 5)
                (move-to-column 12)
                (let ((hover (lsp-request \"textDocument/hover\"
                               (lsp--text-document-position-params))))
                  (if (and hover
                           (gethash \"contents\" hover))
                      t nil)))))
       (svlsp-test/close-file buf)
       result)" \
    "t"
