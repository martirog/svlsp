#!/usr/bin/env bash
# test_62_include_note.sh -- plan.md §6.40 D5: an `include whose file has
# errors says so at the directive. tb.sv includes nt_common.svh, which
# includes nt_macros.svh -- not there. The "cannot find" Error is on
# nt_common.svh, which isn't open; tb.sv used to show only the "undefined
# macro" that follows. Now tb.sv's own directive carries an Information
# note naming the file and the error. Writing nt_macros.svh (reported
# through workspace/didChangeWatchedFiles, as test_56) clears it.
#
# Own workspace in a temp directory, as test_56_include_errors.sh.
# Line reference (0-based): tb.sv line 0 "`include \"nt_common.svh\"" --
# the directive spans chars 0..24.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "a nested include error is a note at the outer directive" \
        "the missing file written on disk clears the note"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "an include whose file has errors says so at the directive (plan.md §6.40 D5)"

NT_WORK="$(mktemp -d /tmp/svlsp-incnote.XXXXXX)"
printf '`include "nt_common.svh"\nmodule nt_m;\n  initial `NT_INFO("hi");\nendmodule\n' \
    > "${NT_WORK}/tb.sv"
printf '`include "nt_macros.svh"\n' > "${NT_WORK}/nt_common.svh"
printf '{\n  "files": ["tb.sv"]\n}\n' > "${NT_WORK}/.svlsp.json"
NT_TB="${NT_WORK}/tb.sv"
NT_MAC="${NT_WORK}/nt_macros.svh"

# svlsp-nt/note: "LINE START END SEVERITY MESSAGE" of tb.sv's note once it
# is there (within 10 s), or tb.sv's messages.
# svlsp-nt/no-note: t once tb.sv has no note (10 s).
_emacs_eval "(progn
  (defun svlsp-nt/diags ()
    (with-current-buffer (get-file-buffer \"${NT_TB}\")
      (gethash (lsp--fix-path-casing \"${NT_TB}\") (lsp-diagnostics t))))
  (defun svlsp-nt/find ()
    (cl-find-if (lambda (d) (string-match-p \"this \\\`include reads\" (lsp:diagnostic-message d)))
                (svlsp-nt/diags)))
  (defun svlsp-nt/note ()
    (let ((deadline (+ (float-time) 10)) d)
      (while (and (not (setq d (svlsp-nt/find))) (< (float-time) deadline)) (sit-for 0.3))
      (if (not d)
          (format \"no note, only: %S\" (mapcar #'lsp:diagnostic-message (svlsp-nt/diags)))
        (let* ((r (lsp:diagnostic-range d)) (s (lsp:range-start r)) (e (lsp:range-end r)))
          (format \"%d %d %d %d %s\" (lsp:position-line s) (lsp:position-character s)
                  (lsp:position-character e) (lsp:diagnostic-severity? d)
                  (lsp:diagnostic-message d))))))
  (defun svlsp-nt/no-note ()
    (let ((deadline (+ (float-time) 10)))
      (while (and (svlsp-nt/find) (< (float-time) deadline)) (sit-for 0.3))
      (not (svlsp-nt/find))))
  t)" >/dev/null

run_test "a nested include error is a note at the outer directive" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${NT_WORK}\")
         (let ((buf (find-file-noselect \"${NT_TB}\")))
           (with-current-buffer buf (normal-mode) (lsp))
           (if (not (with-current-buffer buf (svlsp-test/wait-for-lsp 30)))
               \"no initialized workspace\"
             (with-current-buffer buf
               (let ((lsp-response-timeout 30))
                 (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\"))))
             (svlsp-nt/note))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '"0 0 24 3 this `include reads a file with an error: nt_common.svh:1: `include: cannot find \"nt_macros.svh\""'

printf '`define NT_INFO(m) $display(m)\n' > "${NT_MAC}"

run_test "the missing file written on disk clears the note" \
    "(condition-case err
       (with-current-buffer (get-file-buffer \"${NT_TB}\")
         (lsp-notify \"workspace/didChangeWatchedFiles\"
                     (list :changes (vector (list :uri (lsp--path-to-uri \"${NT_MAC}\") :type 1))))
         (sit-for 1)
         (let ((lsp-response-timeout 30))
           (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\")))
         (if (svlsp-nt/no-note)
             t
           (format \"still: %S\" (mapcar #'lsp:diagnostic-message (svlsp-nt/diags)))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    't'

_emacs_eval "(progn
  (let ((buf (get-file-buffer \"${NT_TB}\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${NT_WORK}\"))
  t)" >/dev/null
rm -rf "${NT_WORK}"
