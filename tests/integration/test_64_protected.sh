#!/usr/bin/env bash
# test_64_protected.sh -- plan.md §6.41: a protected (encrypted) envelope is
# skipped, and a type that doesn't resolve while protected code is in
# reach is an Information note naming the envelope, not a Warning.
# pt_ip.sv: package pt_ip_pkg with an envelope at line 2 (1-based). pt_tb.sv
# imports it and uses pt_hidden_t, which only the envelope could declare.
#
# Own workspace in a temp directory, as test_62_include_note.sh.
# Line reference (0-based): pt_tb.sv line 2 "  pt_hidden_t h;" -- the name
# at char 2.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "an encrypted envelope is no syntax error" \
        "a type protected code may declare is an Information note"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "protected code (plan.md §6.41)"

PT_WORK="$(mktemp -d /tmp/svlsp-protected.XXXXXX)"
cat > "${PT_WORK}/pt_ip.sv" <<'EOF'
package pt_ip_pkg;
`pragma protect begin_protected
`pragma protect encrypt_agent="Some Vendor"
`pragma protect encoding=(enctype="base64", line_length=76, bytes=64)
`pragma protect data_block
aGVsbG8gd29ybGQgdGhpcyBpcyBub3QgcmVhbGx5IGVuY3J5cHRlZCBidXQgbG9va3MgbGlr
ZSBpdA==
`pragma protect end_protected
endpackage
EOF
printf 'module pt_tb;\n  import pt_ip_pkg::*;\n  pt_hidden_t h;\nendmodule\n' > "${PT_WORK}/pt_tb.sv"
printf '{\n  "files": ["pt_ip.sv", "pt_tb.sv"]\n}\n' > "${PT_WORK}/.svlsp.json"
PT_IP="${PT_WORK}/pt_ip.sv"
PT_TB="${PT_WORK}/pt_tb.sv"

# svlsp-pt/diags FILE: FILE's diagnostics as "LINE CHAR SEVERITY MESSAGE".
_emacs_eval "(progn
  (defun svlsp-pt/diags (file)
    (mapcar (lambda (d)
              (let ((s (lsp:range-start (lsp:diagnostic-range d))))
                (format \"%d %d %d %s\" (lsp:position-line s) (lsp:position-character s)
                        (lsp:diagnostic-severity? d) (lsp:diagnostic-message d))))
            (gethash (lsp--fix-path-casing file) (lsp-diagnostics t))))
  (defun svlsp-pt/open (file)
    (let ((buf (find-file-noselect file)))
      (with-current-buffer buf (normal-mode) (lsp))
      (with-current-buffer buf (svlsp-test/wait-for-lsp 30))))
  t)" >/dev/null

run_test "an encrypted envelope is no syntax error" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${PT_WORK}\")
         (if (not (svlsp-pt/open \"${PT_IP}\"))
             \"no initialized workspace\"
           (with-current-buffer (get-file-buffer \"${PT_IP}\")
             (let ((lsp-response-timeout 30))
               (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\")))
             (sit-for 1)
             (or (svlsp-pt/diags \"${PT_IP}\") 'none))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    'none'

run_test "a type protected code may declare is an Information note" \
    "(condition-case err
       (if (not (svlsp-pt/open \"${PT_TB}\"))
           \"no initialized workspace\"
         (with-current-buffer (get-file-buffer \"${PT_TB}\")
           (let ((lsp-response-timeout 30))
             (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\")))
           (let ((deadline (+ (float-time) 10)) ds)
             (while (and (not (setq ds (svlsp-pt/diags \"${PT_TB}\"))) (< (float-time) deadline))
               (sit-for 0.3))
             ds)))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '("2 2 3 unknown type '"'"'pt_hidden_t'"'"'; it may be declared in protected code (pt_ip.sv:2)")'

_emacs_eval "(progn
  (dolist (f (list \"${PT_IP}\" \"${PT_TB}\"))
    (let ((buf (get-file-buffer f)))
      (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf))))
  (ignore-errors (lsp-workspace-folders-remove \"${PT_WORK}\"))
  t)" >/dev/null
rm -rf "${PT_WORK}"
