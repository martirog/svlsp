#!/usr/bin/env bash
# test_13_diagnostics_parse_errors.sh — verify that svlsp publishes real parse
# errors as LSP diagnostics when a document with syntax errors is opened or
# modified, and that diagnostics are cleared when valid content is loaded.
#
# Requires Phase 4.5: the server now runs the full compiler pipeline on every
# didOpen / didChange and publishes ParseError → lsp::Diagnostic results.

SV_FIXTURE_VALID="${SVLSP_ROOT}/examples/module_basic.sv"
SV_FIXTURE_ERROR="${SVLSP_ROOT}/tests/integration/fixtures/syntax_error.sv"

section "diagnostics — parse errors (Phase 4.5)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "zero diagnostics for valid file"      "svlsp binary not found"
    skip_test "diagnostics published for error file" "svlsp binary not found"
    skip_test "diagnostics cleared after fix"        "svlsp binary not found"
else

    # --- valid file → zero diagnostics on didOpen -------------------------
    run_test "zero diagnostics for valid file" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE_VALID}\"))
                   (ok  (with-current-buffer buf
                          (svlsp-test/wait-for-lsp 15)))
                   (count
                     (when ok
                       (with-current-buffer buf
                         (sit-for 2)
                         (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             (if (and ok (eql count 0)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- file with syntax error → non-zero diagnostics on didOpen ---------
    run_test "diagnostics published for error file" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE_ERROR}\"))
                   (ok  (with-current-buffer buf
                          (svlsp-test/wait-for-lsp 15)))
                   (count
                     (when ok
                       (with-current-buffer buf
                         (sit-for 2)
                         (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             (if (and ok (> count 0)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- insert bad syntax → diagnostics appear; revert → diagnostics clear
    run_test "diagnostics cleared after fix" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE_VALID}\"))
                   (ok  (with-current-buffer buf
                          (svlsp-test/wait-for-lsp 15))))
             (when ok
               (with-current-buffer buf
                 ;; Insert a syntax error at the end of the buffer
                 (goto-char (point-max))
                 (insert \"\\n{ bad syntax }\\n\")
                 (sit-for 3)
                 (let ((err-count (length (lsp--get-buffer-diagnostics))))
                   ;; Revert to file content (clears the insertion)
                   (revert-buffer :ignore-auto :noconfirm)
                   (sit-for 3)
                   (let ((clean-count (length (lsp--get-buffer-diagnostics))))
                     (svlsp-test/close-file buf)
                     ;; errors appeared and then cleared
                     (if (and (> err-count 0) (eql clean-count 0)) t nil))))))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

fi
