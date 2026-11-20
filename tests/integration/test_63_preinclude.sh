#!/usr/bin/env bash
# test_63_preinclude.sh -- plan.md §6.40 D8: a config's pre-includes are read
# before every unit, like gcc -include. use.sv uses `PX_INFO, which only
# px_macros.svh defines, and never includes it -- as a project compiled by
# a simulator from a filelist that lists the macros file first. Without
# the option use.sv shows "undefined macro"; with "preincludes" it is clean,
# and the header is logged as read for the project.
#
# Own workspace in a temp directory, as test_56_include_errors.sh.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "a macro only the pre-include defines is not an error" \
        "the pre-include is logged with the file it is read from"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "pre-includes are read before every unit (plan.md §6.40 D8)"

PX_WORK="$(mktemp -d /tmp/svlsp-preinc.XXXXXX)"
mkdir -p "${PX_WORK}/inc"
printf '`define PX_INFO(m) $display(m)\n' > "${PX_WORK}/inc/px_macros.svh"
printf 'module px_m;\n  initial `PX_INFO("hi");\nendmodule\n' > "${PX_WORK}/use.sv"
printf '{\n  "files": ["use.sv"],\n  "includeDirs": ["inc"],\n  "preincludes": ["px_macros.svh"]\n}\n' \
    > "${PX_WORK}/.svlsp.json"
PX_USE="${PX_WORK}/use.sv"

run_test "a macro only the pre-include defines is not an error" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${PX_WORK}\")
         (let ((buf (find-file-noselect \"${PX_USE}\")))
           (with-current-buffer buf (normal-mode) (lsp))
           (if (not (with-current-buffer buf (svlsp-test/wait-for-lsp 30)))
               \"no initialized workspace\"
             (with-current-buffer buf
               (let ((lsp-response-timeout 30))
                 (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\")))
               ;; The open's compile publishes; give it a moment to arrive.
               (sit-for 1)
               (let ((msgs (mapcar #'lsp:diagnostic-message
                                   (gethash (lsp--fix-path-casing \"${PX_USE}\")
                                            (lsp-diagnostics t)))))
                 (if msgs (format \"diagnostics: %S\" msgs) \"clean\"))))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '"clean"'

run_test "the pre-include is logged with the file it is read from" \
    "(condition-case err
       (let ((b (get-buffer \"*lsp-log*\")))
         (if (and b (with-current-buffer b
                      (save-excursion
                        (goto-char (point-min))
                        (search-forward \"pre-include ${PX_WORK}/inc/px_macros.svh read before every unit of ${PX_WORK}/.svlsp.json\" nil t))))
             t
           \"no pre-include line in *lsp-log*\"))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    't'

_emacs_eval "(progn
  (let ((buf (get-file-buffer \"${PX_USE}\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${PX_WORK}\"))
  t)" >/dev/null
rm -rf "${PX_WORK}"
