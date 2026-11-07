#!/usr/bin/env bash
# test_54_project_db.sh -- plan.md §6.34 S3: a project DB built ahead,
# outside the editor (`svlsp --build-db`), is the server's own DB when the
# project's .svlsp.json names it ("database"). The server starts from it:
# hover answers, and nothing is compiled (every compile is a cache hit).
#
# The fixture is written to a temp directory outside the repo and added as
# a workspace folder, so it gets a fresh svlsp process of its own (as
# uvm_corpus/uvm_completion.el does): the shared server of the rest of the
# suite has compiled into memory already and would keep doing so.
#
# Line reference (0-based): b.sv line 2 "  pdb_c obj;" -- pdb_c at char 2.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "--build-db writes the project DB" \
        "hover answers from the prebuilt project DB" \
        "the server compiled nothing (cache hits only)"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "project DB built ahead of the editor (plan.md §6.34 S3)"

PDB_WORK="$(mktemp -d /tmp/svlsp-projdb.XXXXXX)"
printf 'package pdb_p;\n  class pdb_c; endclass\nendpackage\n' > "${PDB_WORK}/a.sv"
printf 'module pdb_m;\n  import pdb_p::*;\n  pdb_c obj;\nendmodule\n' > "${PDB_WORK}/b.sv"
printf '{\n  "files": ["a.sv", "b.sv"],\n  "database": "proj.db"\n}\n' > "${PDB_WORK}/.svlsp.json"
"${SVLSP_BIN}" --build-db "${PDB_WORK}/.svlsp.json" --output "${PDB_WORK}/proj.db" 2>/dev/null

run_test "--build-db writes the project DB" \
    "(file-exists-p \"${PDB_WORK}/proj.db\")" "t"

run_test "hover answers from the prebuilt project DB" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${PDB_WORK}\")
         (let* ((buf (find-file-noselect \"${PDB_WORK}/b.sv\"))
                (svlsp-test-server-args (list \"--log-files\" \"${PDB_WORK}/files.log\")))
           (with-current-buffer buf (normal-mode) (lsp))
           (if (not (with-current-buffer buf (svlsp-test/wait-for-lsp 30)))
               \"no initialized workspace\"
             (let ((deadline (+ (float-time) 30)) result)
               (while (and (null result) (< (float-time) deadline))
                 (setq result
                       (with-current-buffer buf
                         (lsp-request \"textDocument/hover\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position (list :line 2 :character 3)))))
                 (unless result (sit-for 0.5)))
               (if result t \"hover stayed null\")))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Every compile the server ran was a cache hit: no "[parsed] <path>" line
# without "(cached)".
sleep 2
run_test "the server compiled nothing (cache hits only)" \
    "(with-temp-buffer
       (insert-file-contents \"${PDB_WORK}/files.log\")
       (goto-char (point-min))
       (let (compiled)
         (while (re-search-forward \"^\\\\[parsed\\\\] \\\\(/[^ ]*\\\\)\$\" nil t)
           (push (match-string 1) compiled))
         (if compiled (format \"compiled: %s\" compiled) t)))" \
    "t"

_emacs_eval "(progn
  (let ((buf (get-file-buffer \"${PDB_WORK}/b.sv\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${PDB_WORK}\"))
  t)" >/dev/null
rm -rf "${PDB_WORK}"
