#!/usr/bin/env bash
# test_50_library_rename.sh — rename fails closed on a symbol an attached
# library DB declares (plan.md §6.33 C10 follow-up).
#
# A library DB is built with --build-db from lbr_pkg.sv into a temp
# directory; a project there (lbr_tb.sv) attaches it through its own
# .svlsp.json. That directory is its own workspace folder, so it gets its
# own svlsp process, as in uvm_corpus/uvm_completion.el -- the library never
# leaks into the shared server the rest of the suite uses.
#
#   lbr_tb.sv line 4 (0-based), char 14  — `lbr_get_id` (a library method)
#   lbr_tb.sv line 5,           char 9   — `LBR_TAG` (a library macro)
#   lbr_tb.sv line 6,           char 4   — `lbr_local` (the project's own)

section "rename of a library-declared symbol (fails closed)"

LBR_TESTS=(
    "a library method's rename is refused with an error naming the library file"
    "a library macro's rename is refused"
    "a project symbol still renames with the library attached"
)

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in "${LBR_TESTS[@]}"; do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    LBR_WORK="$(mktemp -d /tmp/svlsp-library-rename.XXXXXX)"
    mkdir -p "${LBR_WORK}/lib" "${LBR_WORK}/proj"
    cat > "${LBR_WORK}/lib/lbr_pkg.sv" <<'EOF'
package lbr_pkg;
  `define LBR_TAG(x) x
  class lbr_base;
    function int lbr_get_id(); return 1; endfunction
  endclass
endpackage
EOF
    printf '{\n  "files": ["lbr_pkg.sv"]\n}\n' > "${LBR_WORK}/lib/.svlsp.json"
    cat > "${LBR_WORK}/proj/lbr_tb.sv" <<'EOF'
module lbr_tb;
  import lbr_pkg::*;
  lbr_base b;
  initial begin
    int x = b.lbr_get_id();
    int y = `LBR_TAG(3);
    lbr_local = x + y;
  end
  int lbr_local;
endmodule
EOF
    printf '{\n  "files": ["lbr_tb.sv"],\n  "libraryDbs": ["%s"]\n}\n' \
        "${LBR_WORK}/lib/lbr.db" > "${LBR_WORK}/proj/.svlsp.json"

    if ! "${SVLSP_BIN}" --build-db "${LBR_WORK}/lib/.svlsp.json" \
                        --output "${LBR_WORK}/lib/lbr.db" >/dev/null 2>&1; then
        for name in "${LBR_TESTS[@]}"; do
            skip_test "$name" "--build-db failed for ${LBR_WORK}/lib"
        done
    else
        # Opens the project in its own workspace and waits until the
        # library is loaded (definition of lbr_get_id reaches lbr_pkg.sv).
        _emacs_eval "(progn
          (defvar svlsp-lbr/buf nil)
          (defun svlsp-lbr/rename (line character new-name)
            (with-current-buffer svlsp-lbr/buf
              (lsp-request \"textDocument/rename\"
                           (list :textDocument (list :uri (lsp--buffer-uri))
                                 :position (list :line line :character character)
                                 :newName new-name))))
          (defun svlsp-lbr/rename-error (line character)
            \"The error message renaming at LINE/CHARACTER signals, or nil.\"
            (condition-case err
                (progn (svlsp-lbr/rename line character \"renamed\") nil)
              (error (error-message-string err))))
          (lsp-workspace-folders-add \"${LBR_WORK}/proj\")
          (setq svlsp-lbr/buf (find-file-noselect \"${LBR_WORK}/proj/lbr_tb.sv\"))
          (with-current-buffer svlsp-lbr/buf (normal-mode) (lsp))
          (with-current-buffer svlsp-lbr/buf (svlsp-test/wait-for-lsp 30))
          (let ((deadline (+ (float-time) 30)) found)
            (while (and (not found) (< (float-time) deadline))
              (let ((r (with-current-buffer svlsp-lbr/buf
                         (lsp-request \"textDocument/definition\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position (list :line 4 :character 14))))))
                (setq found (string-match-p \"lbr_pkg\\\\.sv\" (format \"%S\" r))))
              (unless found (sit-for 0.5)))
            t))" >/dev/null

        run_test "${LBR_TESTS[0]}" \
            "(let ((msg (svlsp-lbr/rename-error 4 14)))
               (if (and msg (string-match-p \"lbr_get_id\" msg)
                        (string-match-p \"lib/lbr_pkg\\\\.sv\" msg))
                   t
                 (format \"got %S\" msg)))" \
            "t"

        run_test "${LBR_TESTS[1]}" \
            "(let ((msg (svlsp-lbr/rename-error 5 13)))
               (if (and msg (string-match-p \"LBR_TAG\" msg)) t (format \"got %S\" msg)))" \
            "t"

        run_test "${LBR_TESTS[2]}" \
            "(condition-case err
               (let* ((edit (svlsp-lbr/rename 6 4 \"lbr_total\"))
                      (changes (and edit (gethash \"changes\" edit)))
                      (uris (and changes (hash-table-keys changes))))
                 (if (and (= (length uris) 1)
                          (string-match-p \"proj/lbr_tb\\\\.sv\" (car uris))
                          (= (length (gethash (car uris) changes)) 2))
                     t
                   (format \"got %S\" edit)))
               (error (format \"elisp-error: %s\" (error-message-string err))))" \
            "t"

        _emacs_eval "(progn
          (when (buffer-live-p svlsp-lbr/buf)
            (with-current-buffer svlsp-lbr/buf (set-buffer-modified-p nil))
            (kill-buffer svlsp-lbr/buf))
          (ignore-errors (lsp-workspace-folders-remove \"${LBR_WORK}/proj\"))
          t)" >/dev/null
    fi
    rm -rf "${LBR_WORK}"
fi
