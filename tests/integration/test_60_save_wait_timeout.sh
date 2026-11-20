#!/usr/bin/env bash
# test_60_save_wait_timeout.sh -- plan.md §6.40 D2: after a save, a request
# on the file waits for the save's compile at most saveWaitTimeoutMs, then
# is answered from the previous compile, so a synchronous lsp-mode request
# (completion-at-point, M-.) doesn't freeze Emacs for the whole compile.
# The compile isn't cancelled: its results arrive when it finishes.
#
# Own workspace in a temp directory (as test_56), so its server starts with
# saveWaitTimeoutMs 0: the request right after the save never waits. The
# file is large enough (20000 statements) that its compile is still running
# when the request is handled, with few symbols: lsp-mode asks for
# documentSymbol after an edit (the breadcrumb), and a reply of thousands
# of symbols takes Emacs seconds to read. The save adds class st_new, which
# the previous compile doesn't have: definition on its name finds nothing
# until the save's compile is in.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "a request right after a save is answered from the previous compile" \
        "the save's compile still finishes and its results arrive"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "a save's compile doesn't hold requests past saveWaitTimeoutMs (plan.md §6.40 D2)"

ST_WORK="$(mktemp -d /tmp/svlsp-savewait.XXXXXX)"
python3 -I -c "
print('module st_m;')
print('  int a;')
print('  initial begin')
for i in range(20000):
    print(f'    a = a + {i};')
print('  end')
print('endmodule')" > "${ST_WORK}/big.sv"
printf '{\n  "files": ["big.sv"],\n  "database": ":memory:"\n}\n' > "${ST_WORK}/.svlsp.json"
ST_FILE="${ST_WORK}/big.sv"

# svlsp-st/def-new: whether textDocument/definition on st_new's name (line
# svlsp-st/line, 0-based) finds it.
_emacs_eval "(progn
  (defvar svlsp-st/line 0)
  (defun svlsp-st/def-new ()
    (with-current-buffer (get-file-buffer \"${ST_FILE}\")
      (let ((locs (lsp-request \"textDocument/definition\"
                               (list :textDocument (list :uri (lsp--buffer-uri))
                                     :position (list :line svlsp-st/line :character 7)))))
        (and locs (not (equal locs [])) t))))
  t)" >/dev/null

run_test "a request right after a save is answered from the previous compile" \
    "(condition-case err
       (progn
         (lsp-workspace-folders-add \"${ST_WORK}\")
         (let ((buf (find-file-noselect \"${ST_FILE}\"))
               (svlsp-test/initialization-options (list :svlsp (list :saveWaitTimeoutMs 0))))
           (with-current-buffer buf (normal-mode) (lsp))
           (if (not (with-current-buffer buf (svlsp-test/wait-for-lsp 60)))
               \"no initialized workspace\"
             (with-current-buffer buf
               (let ((lsp-response-timeout 60))
                 (lsp-request \"workspace/executeCommand\" (list :command \"svlsp.waitForIndexing\")))
               (goto-char (point-max))
               (insert \"class st_new; endclass\\n\")
               (setq svlsp-st/line (- (line-number-at-pos (point-max)) 2))
               (save-buffer)
               (let* ((start (float-time))
                      (has (svlsp-st/def-new))
                      (secs (- (float-time) start)))
                 (cond (has (format \"answered after the compile (%.2f s)\" secs))
                       ((> secs 2) (format \"answered late (%.2f s)\" secs))
                       (t \"previous compile\")))))))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    '"previous compile"'

run_test "the save's compile still finishes and its results arrive" \
    "(condition-case err
       (let ((deadline (+ (float-time) 60)) has)
         (while (and (not (setq has (svlsp-st/def-new))) (< (float-time) deadline))
           (sit-for 0.5))
         (if has t \"st_new never appeared\"))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    't'

_emacs_eval "(progn
  (let ((buf (get-file-buffer \"${ST_FILE}\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${ST_WORK}\"))
  t)" >/dev/null
rm -rf "${ST_WORK}"
