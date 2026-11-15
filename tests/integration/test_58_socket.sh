#!/usr/bin/env bash
# test_58_socket.sh -- plan.md §6.38: lsp-mode starts `svlsp --socket`, a
# relay that starts a server for the project on a Unix socket (none
# listens yet) and carries the session over stdio. Ending the editor's
# session leaves the server running; the next session attaches to the
# same server. `svlsp --socket-path DIR` prints the default socket.
#
# Like test_54, the fixture is a temp directory added as a workspace
# folder, so it gets a server process (here: a relay) of its own.
#
# Line reference (0-based): b.sv line 2 "  sk_c obj;" -- sk_c at char 2.

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "--socket-path prints the default socket" \
        "--socket starts a server, hover answers through it" \
        "the server outlives the editor's session" \
        "a second session attaches to the same server"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

section "Unix socket server (plan.md §6.38)"

SK_WORK="$(mktemp -d /tmp/svlsp-sock.XXXXXX)"
printf 'package sk_p;\n  class sk_c; endclass\nendpackage\n' > "${SK_WORK}/a.sv"
printf 'module sk_m;\n  import sk_p::*;\n  sk_c obj;\nendmodule\n' > "${SK_WORK}/b.sv"
printf '{\n  "files": ["a.sv", "b.sv"],\n  "database": ":memory:"\n}\n' > "${SK_WORK}/.svlsp.json"
SK_SOCK="${SK_WORK}/s.sock"
SK_LOG="${SK_WORK}/s.log"

run_test "--socket-path prints the default socket" \
    "(let ((out (string-trim (shell-command-to-string
                               \"${SVLSP_BIN} --socket-path ${SK_WORK}\"))))
       (if (string-match-p (format \"\\\\\`/tmp/svlsp-%d/[0-9a-f]\\\\{16\\\\}\\\\.sock\\\\'\"
                                   (user-uid))
                           out)
           t out))" \
    "t"

# Opens b.sv in the fixture's workspace through `svlsp --socket` and
# returns hover's answer at sk_c (t when it answers).
SK_SESSION="(condition-case err
   (progn
     (lsp-workspace-folders-add \"${SK_WORK}\")
     (let* ((buf (find-file-noselect \"${SK_WORK}/b.sv\"))
            (svlsp-test-server-args (list \"--socket\" \"${SK_SOCK}\")))
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
 (error (format \"elisp-error: %s\" (error-message-string err))))"

# Ends the session: the buffer closed, the folder removed (its workspace
# shut down); then waits (up to 15 s) for the server to have logged N
# detaches, and returns how many it has.
_emacs_eval "(defun svlsp-test/sk-end (n)
  (let ((buf (get-file-buffer \"${SK_WORK}/b.sv\")))
    (when buf (with-current-buffer buf (set-buffer-modified-p nil)) (kill-buffer buf)))
  (ignore-errors (lsp-workspace-folders-remove \"${SK_WORK}\"))
  (let ((deadline (+ (float-time) 15)) (got 0))
    (while (and (< (float-time) deadline)
                (< (setq got (svlsp-test/sk-count \"editor detached\")) n))
      (sit-for 0.2))
    got))" >/dev/null

_emacs_eval "(defun svlsp-test/sk-count (what)
  (if (not (file-exists-p \"${SK_LOG}\")) 0
    (with-temp-buffer
      (insert-file-contents \"${SK_LOG}\")
      (how-many (regexp-quote what) (point-min) (point-max)))))" >/dev/null

run_test "--socket starts a server, hover answers through it" "${SK_SESSION}" "t"

run_test "the server outlives the editor's session" \
    "(let ((n (svlsp-test/sk-end 1)))
       (sit-for 1)
       (list n (file-exists-p \"${SK_SOCK}\") (svlsp-test/sk-count \"listening on\")))" \
    "(1 t 1)"

run_test "a second session attaches to the same server" \
    "(let ((r ${SK_SESSION}))
       (list r (svlsp-test/sk-count \"listening on\") (svlsp-test/sk-count \"editor attached\")))" \
    "(t 1 2)"

_emacs_eval "(svlsp-test/sk-end 2)" >/dev/null
# Stop the server this test started (its pid is in its log).
SK_PID="$(sed -n 's/.*listening on .* (pid \([0-9]*\)).*/\1/p' "${SK_LOG}" | head -1)"
[ -n "${SK_PID}" ] && kill -TERM "${SK_PID}" 2>/dev/null
sleep 0.5
rm -rf "${SK_WORK}"
