#!/usr/bin/env bash
# test_55_watched_files.sh -- plan.md §6.34 S6: a file changed on disk
# outside the editor (a checkout) refreshes the project once the client
# reports it (workspace/didChangeWatchedFiles; the suite turns lsp-mode's
# own watchers off, so the test sends it): definition follows the moved
# declaration, and a declaration removed from an unopened file flags its
# use in the open one.
#
# Own workspace in a temp directory, as test_54_project_db.sh.
# Line reference (0-based): use.sv line 2 "  wf_c obj;" -- wf_c at char 2.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "definition before the change" \
        "definition follows the declaration moved on disk" \
        "a declaration removed on disk flags its use"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "files changed on disk refresh the project (plan.md §6.34 S6)"

WF_WORK="$(mktemp -d /tmp/svlsp-watch.XXXXXX)"
printf 'package wf_p;\n  class wf_c; endclass\nendpackage\n' > "${WF_WORK}/pkg.sv"
printf 'module wf_m;\n  import wf_p::*;\n  wf_c obj;\nendmodule\n' > "${WF_WORK}/use.sv"
printf '{\n  "files": ["pkg.sv", "use.sv"]\n}\n' > "${WF_WORK}/.svlsp.json"

# Definition line of wf_c, or a string saying what went wrong.
WF_DEF="(let ((locs (with-current-buffer (get-file-buffer \"${WF_WORK}/use.sv\")
                    (lsp-request \"textDocument/definition\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position (list :line 2 :character 3))))))
          (let ((loc (if (vectorp locs) (and (> (length locs) 0) (aref locs 0))
                       (if (and (listp locs) (not (hash-table-p locs))) (car locs) locs))))
            (if loc (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))) \"no location\")))"

# Tells the server pkg.sv changed and waits for its refresh.
WF_NOTIFY="(with-current-buffer (get-file-buffer \"${WF_WORK}/use.sv\")
             (lsp-notify \"workspace/didChangeWatchedFiles\"
                         (list :changes (vector (list :uri (lsp--path-to-uri \"${WF_WORK}/pkg.sv\")
                                                      :type 2))))
             (sit-for 1)
             (let ((lsp-response-timeout 30))
               (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\")))
             t)"

run_test "definition before the change" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${WF_WORK}\")
         (let ((buf (find-file-noselect \"${WF_WORK}/use.sv\")))
           (with-current-buffer buf (normal-mode) (lsp))
           (if (not (with-current-buffer buf (svlsp-test/wait-for-lsp 30)))
               \"no initialized workspace\"
             ${WF_DEF})))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "1"

printf 'package wf_p;\n\n\n  class wf_c; endclass\nendpackage\n' > "${WF_WORK}/pkg.sv"
run_test "definition follows the declaration moved on disk" \
    "(condition-case err (progn ${WF_NOTIFY} ${WF_DEF})
       (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "3"

printf 'package wf_p;\nendpackage\n' > "${WF_WORK}/pkg.sv"
run_test "a declaration removed on disk flags its use" \
    "(condition-case err
       (progn ${WF_NOTIFY}
         (let ((deadline (+ (float-time) 10)) found)
           (while (and (not found) (< (float-time) deadline))
             (with-current-buffer (get-file-buffer \"${WF_WORK}/use.sv\")
               (setq found (cl-some (lambda (d) (string-match-p \"unknown type 'wf_c'\" (lsp:diagnostic-message d)))
                                    (gethash (lsp--fix-path-casing \"${WF_WORK}/use.sv\")
                                             (lsp-diagnostics t)))))
             (unless found (sit-for 0.3)))
           (if found t \"no unknown-type warning\")))
       (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

_emacs_eval "(progn
  (let ((buf (get-file-buffer \"${WF_WORK}/use.sv\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${WF_WORK}\"))
  t)" >/dev/null
rm -rf "${WF_WORK}"
