#!/usr/bin/env bash
# test_38_cross_file_invalidation.sh — verify plan.md §6.4: saving a file
# force-recompiles every other file whose own compiled unit `` `include ``s
# it, through a real Emacs/lsp-mode client, not just the direct pipe-driven
# proof in tests/unit/lsp/test_server_cross_file_invalidation.cpp.
#
# Uses two scratch temp files (not tracked fixtures, since this test edits
# and saves real buffers): A (included) and B (`` `include ``s A). Both live
# outside SVLSP_ROOT, so they get their own isolated single-file workspace.
#
# `lsp-diagnostics-updated-hook` runs once per textDocument/publishDiagnostics
# notification lsp-mode actually receives, regardless of content -- dynamically
# shadowing it with a counting function for the duration of the edit+save is a
# reliable, client-visible way to prove *more than one* file got recompiled as
# a result of a single save, without needing to inspect raw wire messages the
# way the unit test does. A plain single-file save (no dependents) would only
# ever produce one such call; seeing at least two here is exactly the signal
# that B's own force-recompile (fired asynchronously off
# m_dependencyRechecker, per this section's own design) actually happened.

section "cross-file invalidation (plan.md §6.4)"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "saving an included file causes more than one diagnostics update to arrive" \
        "svlsp binary not found at ${SVLSP_BIN}"
else
    run_test "saving an included file causes more than one diagnostics update to arrive" \
        "(condition-case err
           (let* ((a-tmp (make-temp-file \"svlsp_cfi_a_\" nil \".sv\"
                                          \"module a_mod; endmodule\\n\"))
                  (b-tmp (make-temp-file \"svlsp_cfi_b_\" nil \".sv\"
                                          (format \"\`include \\\"%s\\\"\\nmodule b_mod; endmodule\\n\"
                                                  a-tmp)))
                  (buf-a (svlsp-test/open-file a-tmp))
                  (ok-a  (with-current-buffer buf-a (svlsp-test/wait-for-lsp 15))))
             (unwind-protect
                 (when ok-a
                   (with-current-buffer buf-a (sit-for 1))
                   (let* ((buf-b (svlsp-test/open-file b-tmp))
                          (ok-b  (with-current-buffer buf-b (svlsp-test/wait-for-lsp 15))))
                     (unwind-protect
                         (when ok-b
                           (with-current-buffer buf-b (sit-for 1))
                           (let* ((update-count 0)
                                  (lsp-diagnostics-updated-hook
                                    (list (lambda () (setq update-count (1+ update-count))))))
                             (with-current-buffer buf-a
                               (goto-char (point-max))
                               (insert \"\\n{ bad token }\\n\")
                               (save-buffer))
                             (let ((deadline (+ (float-time) 3)))
                               (while (and (< update-count 2) (< (float-time) deadline))
                                 (sit-for 0.1)))
                             (>= update-count 2)))
                       (svlsp-test/close-file buf-b))))
               (svlsp-test/close-file buf-a)
               (delete-file a-tmp)
               (delete-file b-tmp)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
