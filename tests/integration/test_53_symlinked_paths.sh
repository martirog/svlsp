#!/usr/bin/env bash
# test_53_symlinked_paths.sh — a file opened through a symlinked directory
# keeps the client's path in every URI the server sends back, through a
# real Emacs/lsp-mode client (unit side:
# tests/unit/lsp/test_server_cross_file_invalidation.cpp, "[symlink]").
#
# Emacs (find-file-visit-truename off, the default) and lsp-mode send the
# path as opened, symlink included. The server used to resolve symlinks
# when turning a stored path back into a URI, so an included file's
# diagnostics and a dependency recompile went to the real path, and the
# recompile read the disk copy instead of the buffer. Here the disk copy
# has a syntax error the buffer has removed (unsaved), so the old server
# left diagnostics under the real directory, which nothing opened.
# (lsp-mode drops a file whose diagnostics are empty, so a misrouted clean
# publish would leave no trace.)
#
# Scratch files in a temp dir (this test edits and saves), names prefixed
# symlnk_.

section "symlinked paths keep the client's URI"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "a symlinked file's diagnostics stay under the symlinked path" \
        "svlsp binary not found at ${SVLSP_BIN}"
else
    run_test "a symlinked file's diagnostics stay under the symlinked path" \
        "(condition-case err
           (let* ((real (make-temp-file \"svlsp_symlnk_real_\" t))
                  (link (concat real \"_link\"))
                  (_    (make-symbolic-link real link))
                  (_    (with-temp-file (expand-file-name \"symlnk_inc.svh\" real)
                          (insert \"module symlnk_inc; endmodule\\n\")))
                  (_    (with-temp-file (expand-file-name \"symlnk_top.sv\" real)
                          (insert \"\`include \\\"symlnk_inc.svh\\\"\\nmodule symlnk_top; endmodule\\nmodule symlnk_bad { endmodule\\n\")))
                  (top-path (expand-file-name \"symlnk_top.sv\" link))
                  (inc-path (expand-file-name \"symlnk_inc.svh\" link))
                  (buf-top (svlsp-test/open-file top-path))
                  (ok-top  (with-current-buffer buf-top (svlsp-test/wait-for-lsp 15)))
                  (buf-inc nil))
             (unwind-protect
                 (when ok-top
                   ;; Remove the disk copy's syntax error, unsaved.
                   (with-current-buffer buf-top
                     (sit-for 1)
                     (goto-char (point-min))
                     (search-forward \"module symlnk_bad\")
                     (delete-region (line-beginning-position) (line-end-position))
                     (sit-for 1.5))
                   (setq buf-inc (svlsp-test/open-file inc-path))
                   (with-current-buffer buf-inc
                     (svlsp-test/wait-for-lsp 15)
                     (sit-for 1))
                   ;; The save recompiles the top file (debounced): wait
                   ;; for its publish too, as test_38 does.
                   (let* ((update-count 0)
                          (lsp-diagnostics-updated-hook
                            (list (lambda () (setq update-count (1+ update-count))))))
                     (with-current-buffer buf-inc
                       (goto-char (point-max))
                       (insert \"// saved\\n\")
                       (save-buffer))
                     (let ((deadline (+ (float-time) 5)))
                       (while (and (< update-count 2) (< (float-time) deadline))
                         (sit-for 0.1)))
                     (sit-for 0.5)
                     (unless (>= update-count 2)
                       (error \"no recompile after the save (%d updates)\" update-count)))
                   (let* ((diags (lsp-diagnostics))
                          (real-dir (file-name-as-directory real))
                          (real-keys nil)
                          (top-count (length (gethash top-path diags))))
                     (maphash (lambda (k _)
                                (when (string-prefix-p real-dir k) (push k real-keys)))
                              diags)
                     (if (and (null real-keys) (= top-count 0))
                         t
                       (list :real-keys real-keys :top-count top-count))))
               (when buf-inc (svlsp-test/close-file buf-inc))
               (svlsp-test/close-file buf-top)
               (delete-file link)
               (delete-directory real t)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
