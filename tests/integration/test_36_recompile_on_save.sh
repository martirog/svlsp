#!/usr/bin/env bash
# test_36_recompile_on_save.sh — verify plan.md §6.18: textDocument/didSave
# triggers an immediate recompile+publish, not just §6.8's debounced
# didChange (a ~300ms quiet-period timer). Before this feature, an edit
# followed immediately by a save (well inside that window) could briefly
# show pre-edit diagnostics until the debounce timer eventually fired.
#
# Uses a scratch temp .sv file (not a tracked fixture) since this test
# actually saves the buffer to disk — every other test in this suite only
# edits the in-memory buffer and closes it unsaved (svlsp-test/close-file
# clears the modified flag first). The temp file lives outside SVLSP_ROOT
# (no `.svlsp.json`/`.svlsp.f` in its ancestry), so it gets its own
# single-file workspace/server process, isolated from the shared
# SVLSP_ROOT-rooted workspace every other test in this suite reuses. It is
# removed at the end of each case regardless of outcome.
#
# `lsp-idle-delay` (client-side: how long lsp-mode waits for typing to pause
# before it sends a buffered didChange at all) defaults to 0.5s in this
# suite's init file — longer than the server's own 300ms debounce, which
# would make "insert, then save immediately" never exercise the interesting
# race at all (didChange itself wouldn't even be on the wire yet by the time
# the poll window below closes). It must be lowered *before* the buffer's
# lsp-mode connection is established (svlsp-test/open-file calls `(lsp)`,
# which captures the current value when it creates its idle timer) to
# actually simulate a fast typist saving right after an edit; restored
# unconditionally afterward since it's a global defcustom.
#
# Timing: insert a bad token, briefly let the (now-fast) idle timer flush
# the resulting didChange, then save-buffer (triggers textDocument/didSave
# via lsp-mode's own after-save-hook) and poll diagnostics for up to 250ms —
# strictly less than ChangeDebouncer's default 300ms delay, so diagnostics
# arriving inside that budget can only be explained by didSave's own
# immediate compileAndPublish path, not the pending debounced didChange
# timer catching up on its own (confirmed empirically: real end-to-end
# latency for this path is ~200-220ms).

section "recompile on save (plan.md §6.18)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "didSave publishes updated diagnostics well inside the debounce window" \
        "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "a clean save after fixing an error clears diagnostics promptly too" \
        "svlsp binary not found at ${SVLSP_BIN}"
else
    run_test "didSave publishes updated diagnostics well inside the debounce window" \
        "(condition-case err
           (let ((old-delay lsp-idle-delay))
             (setq lsp-idle-delay 0.01)
             (unwind-protect
                 (let* ((tmp (make-temp-file \"svlsp_save_test_\" nil \".sv\"
                                              \"module m; endmodule\\n\"))
                        (buf (svlsp-test/open-file tmp))
                        (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
                   (unwind-protect
                       (when ok
                         (with-current-buffer buf
                           (sit-for 1) ; let the initial didOpen compile settle
                           (goto-char (point-max))
                           (insert \"\\n{ bad token }\\n\")
                           (sit-for 0.1) ; let the fast idle timer flush didChange
                           (save-buffer)
                           (let ((deadline (+ (float-time) 0.25)))
                             (while (and (= (length (lsp--get-buffer-diagnostics)) 0)
                                         (< (float-time) deadline))
                               (sit-for 0.01)))
                           (> (length (lsp--get-buffer-diagnostics)) 0)))
                     (svlsp-test/close-file buf)
                     (delete-file tmp)))
               (setq lsp-idle-delay old-delay)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "a clean save after fixing an error clears diagnostics promptly too" \
        "(condition-case err
           (let ((old-delay lsp-idle-delay))
             (setq lsp-idle-delay 0.01)
             (unwind-protect
                 (let* ((tmp (make-temp-file \"svlsp_save_test_\" nil \".sv\"
                                              \"module m; endmodule\\n\"))
                        (buf (svlsp-test/open-file tmp))
                        (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
                   (unwind-protect
                       (when ok
                         (with-current-buffer buf
                           (sit-for 1)
                           (goto-char (point-max))
                           (insert \"\\n{ bad token }\\n\")
                           (sit-for 0.1)
                           (save-buffer)
                           (let ((deadline (+ (float-time) 2)))
                             (while (and (= (length (lsp--get-buffer-diagnostics)) 0)
                                         (< (float-time) deadline))
                               (sit-for 0.02)))
                           (let ((err-n (length (lsp--get-buffer-diagnostics))))
                             (goto-char (point-max))
                             (search-backward \"{ bad token }\")
                             (replace-match \"\")
                             (sit-for 0.1)
                             (save-buffer)
                             (let ((deadline (+ (float-time) 0.25)))
                               (while (and (> (length (lsp--get-buffer-diagnostics)) 0)
                                           (< (float-time) deadline))
                                 (sit-for 0.01)))
                             (and (> err-n 0) (= (length (lsp--get-buffer-diagnostics)) 0)))))
                     (svlsp-test/close-file buf)
                     (delete-file tmp)))
               (setq lsp-idle-delay old-delay)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
