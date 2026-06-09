#!/usr/bin/env bash
# test_14_incremental_parsing.sh — verify Phase 4.6 incremental parse cache.
#
# The cache maps URI → {content_hash, WalkResult}.  The observable effects are:
#   1. Diagnostics are correct on a fresh open (cache miss → full parse).
#   2. Diagnostics are still correct after close and reopen (cache evicted on
#      close, repopulated on next open).
#   3. Changing content updates diagnostics (cache miss on new hash → re-parse).
#   4. Reverting content restores original diagnostics (cache miss on original
#      hash after the intermediate version was stored → re-parse to clean).
#
# These tests also serve as a regression guard: any crash inside parseDiagnostics
# or the cache would surface here before integration with the LSP client.

SV_VALID="${SVLSP_ROOT}/examples/module_basic.sv"
SV_ERROR="${SVLSP_ROOT}/tests/integration/fixtures/syntax_error.sv"

section "incremental parsing — parse cache (Phase 4.6)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "cache miss on first open — valid file has zero diagnostics" "svlsp binary not found"
    skip_test "cache evicted on close — reopen still correct"              "svlsp binary not found"
    skip_test "new content triggers re-parse — errors appear"              "svlsp binary not found"
    skip_test "revert triggers re-parse — errors cleared"                  "svlsp binary not found"
else

    # --- 1. Cache miss on first open: clean file → zero diagnostics ------
    run_test "cache miss on first open — valid file has zero diagnostics" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_VALID}\"))
                   (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                   (n   (when ok (with-current-buffer buf
                                   (sit-for 2)
                                   (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             (if (and ok (eql n 0)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- 2. Cache evicted on close — reopen produces correct result -------
    run_test "cache evicted on close — reopen still correct" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_ERROR}\"))
                   (ok1 (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                   (n1  (when ok1 (with-current-buffer buf
                                    (sit-for 2)
                                    (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             ;; Reopen the same file — cache was evicted, so the server re-parses
             (let* ((buf2 (svlsp-test/open-file \"${SV_ERROR}\"))
                     (ok2  (with-current-buffer buf2 (svlsp-test/wait-for-lsp 15)))
                     (n2   (when ok2 (with-current-buffer buf2
                                       (sit-for 2)
                                       (length (lsp--get-buffer-diagnostics))))))
               (svlsp-test/close-file buf2)
               (if (and ok1 ok2 (> n1 0) (> n2 0)) t nil)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- 3. New content triggers re-parse — errors appear ----------------
    run_test "new content triggers re-parse — errors appear" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_VALID}\"))
                   (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
             (when ok
               (with-current-buffer buf
                 (sit-for 2)
                 (goto-char (point-max))
                 (insert \"\\n{ bad token }\\n\")
                 (sit-for 3)
                 (let ((n (length (lsp--get-buffer-diagnostics))))
                   (svlsp-test/close-file buf)
                   (if (> n 0) t nil)))))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- 4. Revert triggers re-parse — errors cleared --------------------
    run_test "revert triggers re-parse — errors cleared" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_VALID}\"))
                   (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
             (when ok
               (with-current-buffer buf
                 (goto-char (point-max))
                 (insert \"\\n{ bad token }\\n\")
                 (sit-for 3)
                 (let ((err-n (length (lsp--get-buffer-diagnostics))))
                   (revert-buffer :ignore-auto :noconfirm)
                   (sit-for 3)
                   (let ((clean-n (length (lsp--get-buffer-diagnostics))))
                     (svlsp-test/close-file buf)
                     (if (and (> err-n 0) (eql clean-n 0)) t nil))))))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

fi
