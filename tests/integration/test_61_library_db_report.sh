#!/usr/bin/env bash
# test_61_library_db_report.sh -- plan.md §6.40 D1: a project's library DBs
# are reported. One that is attached is logged with its counts (window/
# logMessage, lsp-mode's *lsp-log*); one that can't be used is a warning
# (window/showMessage, shown in the echo area and *Messages*) naming the
# config that lists it. A wrong path used to attach an empty file created
# on the spot, with nothing said.
#
# Own workspace in a temp directory (as test_56); the library is built
# with the binary under test's --build-db.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "a library DB that does not exist is a warning naming its config" \
        "an attached library DB is logged with its counts"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "library DBs are reported (plan.md §6.40 D1)"

LR_WORK="$(mktemp -d /tmp/svlsp-libreport.XXXXXX)"
mkdir -p "${LR_WORK}/lib" "${LR_WORK}/proj"
printf 'package lr_lib;\n  class lr_c; endclass\nendpackage\n' > "${LR_WORK}/lib/lib_pkg.sv"
printf '{"files": ["lib_pkg.sv"]}\n' > "${LR_WORK}/lib/lib.json"
"${SVLSP_BIN}" --build-db "${LR_WORK}/lib/lib.json" --output "${LR_WORK}/lib/lib.db" >/dev/null 2>&1
printf 'module lr_m; import lr_lib::*; lr_c c; endmodule\n' > "${LR_WORK}/proj/use.sv"
printf '{\n  "files": ["use.sv"],\n  "database": ":memory:",\n  "libraryDbs": ["../lib/lib.db", "../lib/missing.db"]\n}\n' \
    > "${LR_WORK}/proj/.svlsp.json"
LR_USE="${LR_WORK}/proj/use.sv"

# svlsp-lr/buffer-has: whether BUFFER-NAME's text contains STRING.
_emacs_eval "(progn
  (defun svlsp-lr/buffer-has (buffer-name string)
    (let ((b (get-buffer buffer-name)))
      (and b (with-current-buffer b
               (save-excursion (goto-char (point-min)) (search-forward string nil t)))
           t)))
  t)" >/dev/null

run_test "a library DB that does not exist is a warning naming its config" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${LR_WORK}/proj\")
         (let ((buf (find-file-noselect \"${LR_USE}\")))
           (with-current-buffer buf (normal-mode) (lsp))
           (if (not (with-current-buffer buf (svlsp-test/wait-for-lsp 30)))
               \"no initialized workspace\"
             (with-current-buffer buf
               (let ((lsp-response-timeout 30))
                 (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\"))))
             (sit-for 0.5)
             (cond ((not (svlsp-lr/buffer-has \"*Messages*\"
                           \"library DB ${LR_WORK}/lib/missing.db (listed in ${LR_WORK}/proj/.svlsp.json) does not exist\"))
                    \"no warning in *Messages*\")
                   ((file-exists-p \"${LR_WORK}/lib/missing.db\") \"missing.db was created\")
                   (t t)))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    't'

run_test "an attached library DB is logged with its counts" \
    "(condition-case err
       (if (svlsp-lr/buffer-has \"*lsp-log*\" \"attached library DB ${LR_WORK}/lib/lib.db (1 files, \")
           t
         \"no attach line in *lsp-log*\")
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    't'

_emacs_eval "(progn
  (let ((buf (get-file-buffer \"${LR_USE}\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${LR_WORK}/proj\"))
  t)" >/dev/null
rm -rf "${LR_WORK}"
