#!/usr/bin/env bash
# test_59_symlink_include_context.sh -- plan.md §6.40 D6: one name per file.
# The project's config names the package through a symlinked directory
# (alias/pkg.sv); Emacs opens the class file the package `include's by its
# real path (real/cls.svh). svlsp used to record the include under the
# symlinked name, so the opened file found no includer: compiled on its
# own, without the package's macro, it showed "undefined macro". Paths are
# canonical inside svlsp now, and URIs sent back keep the client's names.
#
# Own workspace in a temp directory, as test_56_include_errors.sh.

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "an included file opened by its real path keeps its includer's macros" \
        "svlsp binary not found at ${SVLSP_BIN}"
    return
fi

section "symlinked include context (plan.md §6.40 D6)"

SC_WORK="$(mktemp -d /tmp/svlsp-symctx.XXXXXX)"
mkdir -p "${SC_WORK}/real"
printf '`define SC_DECL(n) int n;\npackage sc_p;\n  `include "cls.svh"\nendpackage\n' \
    > "${SC_WORK}/real/pkg.sv"
printf 'class sc_c;\n  `SC_DECL(v)\nendclass\n' > "${SC_WORK}/real/cls.svh"
ln -s "${SC_WORK}/real" "${SC_WORK}/alias"
printf '{\n  "files": ["alias/pkg.sv"],\n  "database": ":memory:"\n}\n' > "${SC_WORK}/.svlsp.json"
SC_CLS="${SC_WORK}/real/cls.svh"

run_test "an included file opened by its real path keeps its includer's macros" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${SC_WORK}\")
         (let ((buf (find-file-noselect \"${SC_CLS}\")))
           (with-current-buffer buf (normal-mode) (lsp))
           (if (not (with-current-buffer buf (svlsp-test/wait-for-lsp 30)))
               \"no initialized workspace\"
             (with-current-buffer buf
               (let ((lsp-response-timeout 30))
                 (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\")))
               (let ((deadline (+ (float-time) 10)) msgs)
                 (while (progn
                          (setq msgs (mapcar #'lsp:diagnostic-message
                                             (gethash (lsp--fix-path-casing \"${SC_CLS}\")
                                                      (lsp-diagnostics t))))
                          (and msgs (< (float-time) deadline)))
                   (sit-for 0.3))
                 (let ((alias-keys nil))
                   (maphash (lambda (k _)
                              (when (string-prefix-p \"${SC_WORK}/alias/\" k) (push k alias-keys)))
                            (lsp-diagnostics t))
                   (cond (msgs (format \"diagnostics: %S\" msgs))
                         (alias-keys (format \"published under the alias: %S\" alias-keys))
                         (t \"clean\"))))))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '"clean"'

_emacs_eval "(progn
  (let ((buf (get-file-buffer \"${SC_CLS}\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${SC_WORK}\"))
  t)" >/dev/null
rm -rf "${SC_WORK}"
