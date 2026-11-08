#!/usr/bin/env bash
# test_56_include_errors.sh -- plan.md §6.35 P3: an `include of a file that
# isn't there is an Error at the directive, over the directive. Writing the
# file (reported through workspace/didChangeWatchedFiles, as test_55)
# clears it and its symbols resolve; deleting it again brings the error
# back.
#
# Own workspace in a temp directory, as test_55_watched_files.sh.
# Line reference (0-based): top.sv line 1 "  `include \"ie_missing.svh\"" --
# the directive spans chars 2..27; line 5 "  ie_c obj;" -- ie_c at char 2;
# line 6 "  int ie_v = `IE_UNDEF;" -- an undefined macro (P4), chars 2..23.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "a missing include is an Error over its directive" \
        "the file written on disk clears it and its class resolves" \
        "the file deleted on disk brings it back" \
        "an undefined macro is an Error over its line"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "include and preprocessor errors are diagnostics (plan.md §6.35)"

IE_WORK="$(mktemp -d /tmp/svlsp-incerr.XXXXXX)"
printf 'package ie_p;\n  `include "ie_missing.svh"\nendpackage\nmodule ie_m;\n  import ie_p::*;\n  ie_c obj;\n  int ie_v = `IE_UNDEF;\nendmodule\n' \
    > "${IE_WORK}/top.sv"
printf '{\n  "files": ["top.sv"]\n}\n' > "${IE_WORK}/.svlsp.json"
IE_TOP="${IE_WORK}/top.sv"
IE_INC="${IE_WORK}/ie_missing.svh"

# svlsp-ie/include-diag: "LINE START END SEVERITY" of top.sv's "cannot find"
# diagnostic once one is there (within 10 s), or nil.
# svlsp-ie/no-include-diag: t once top.sv has no such diagnostic (10 s).
# svlsp-ie/notify: reports TYPE (1 created, 3 deleted) for the include and
# waits for the refresh.
_emacs_eval "(progn
  (defun svlsp-ie/find (&optional re)
    (cl-find-if (lambda (d) (string-match-p (or re \"cannot find \\\"ie_missing.svh\\\"\")
                                            (lsp:diagnostic-message d)))
                (svlsp-ie/top-diags)))
  ;; (lsp-diagnostics t) is the current buffer's workspace's.
  (defun svlsp-ie/top-diags ()
    (with-current-buffer (get-file-buffer \"${IE_TOP}\")
      (gethash (lsp--fix-path-casing \"${IE_TOP}\") (lsp-diagnostics t))))
  (defun svlsp-ie/messages ()
    (format \"no include diagnostic, only: %S\"
            (mapcar #'lsp:diagnostic-message
                    (svlsp-ie/top-diags))))
  (defun svlsp-ie/include-diag (&optional re)
    (let ((deadline (+ (float-time) 10)) d)
      (while (and (not (setq d (svlsp-ie/find re))) (< (float-time) deadline)) (sit-for 0.3))
      (when d
        (let* ((r (lsp:diagnostic-range d)) (s (lsp:range-start r)) (e (lsp:range-end r)))
          (format \"%d %d %d %d\" (lsp:position-line s) (lsp:position-character s)
                  (lsp:position-character e) (lsp:diagnostic-severity? d))))))
  (defun svlsp-ie/no-include-diag ()
    (let ((deadline (+ (float-time) 10)))
      (while (and (svlsp-ie/find) (< (float-time) deadline)) (sit-for 0.3))
      (not (svlsp-ie/find))))
  (defun svlsp-ie/notify (type)
    (with-current-buffer (get-file-buffer \"${IE_TOP}\")
      (lsp-notify \"workspace/didChangeWatchedFiles\"
                  (list :changes (vector (list :uri (lsp--path-to-uri \"${IE_INC}\") :type type))))
      (sit-for 1)
      (let ((lsp-response-timeout 30))
        (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\")))
      t))
  t)" >/dev/null

# Definition of ie_c at its use: \"FILE LINE\", or what went wrong.
IE_DEF="(let ((locs (with-current-buffer (get-file-buffer \"${IE_TOP}\")
                    (lsp-request \"textDocument/definition\"
                                 (list :textDocument (list :uri (lsp--buffer-uri))
                                       :position (list :line 5 :character 3))))))
          (let ((loc (if (vectorp locs) (and (> (length locs) 0) (aref locs 0))
                       (if (and (listp locs) (not (hash-table-p locs))) (car locs) locs))))
            (if loc
                (format \"%s %d\" (file-name-nondirectory (lsp--uri-to-path (gethash \"uri\" loc)))
                        (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))))
              \"no location\")))"

run_test "a missing include is an Error over its directive" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${IE_WORK}\")
         (let ((buf (find-file-noselect \"${IE_TOP}\")))
           (with-current-buffer buf (normal-mode) (lsp))
           (if (not (with-current-buffer buf (svlsp-test/wait-for-lsp 30)))
               \"no initialized workspace\"
             (or (svlsp-ie/include-diag) (svlsp-ie/messages)))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '"1 2 27 1"'

printf 'class ie_c; endclass\n' > "${IE_INC}"
run_test "the file written on disk clears it and its class resolves" \
    "(condition-case err
       (progn (svlsp-ie/notify 1)
         (if (svlsp-ie/no-include-diag) ${IE_DEF} \"include diagnostic still there\"))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '"ie_missing.svh 0"'

rm -f "${IE_INC}"
run_test "the file deleted on disk brings it back" \
    "(condition-case err
       (progn (svlsp-ie/notify 3)
         (or (svlsp-ie/include-diag) (svlsp-ie/messages)))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '"1 2 27 1"'

run_test "an undefined macro is an Error over its line" \
    "(condition-case err
       (or (svlsp-ie/include-diag \"undefined macro \`IE_UNDEF\`\") (svlsp-ie/messages))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '"6 2 23 1"'

_emacs_eval "(progn
  (let ((buf (get-file-buffer \"${IE_TOP}\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${IE_WORK}\"))
  t)" >/dev/null
rm -rf "${IE_WORK}"
