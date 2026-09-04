#!/usr/bin/env bash
# test_35_fuzzy_completion_toggle.sh — verify plan.md §6.11's
# `initializationOptions.svlsp.fuzzyCompletion` flag actually reaches
# CompletionProvider through the real `initialize` handshake, not just at
# the ServerState/CompletionProvider unit-test level.
#
# Reuses the fuzzy_completion.sv fixture (module "sensor": WIDTH parameter,
# report_id/xxrepxx ports; see test_25_completion_fuzzy.sh's own header for
# the exact probe-line layout) — same positions, opposite expectation.
#
# Since every fixture in this repo resolves to the same git-root workspace
# and lsp-mode reuses one server process for it (see test_21's own header
# comment on this), changing `initializationOptions` for an already-running
# workspace requires tearing it down and reconnecting -- unlike test_21,
# this section actually needs that (it's the only way to observe the flag's
# effect at all). Rather than `lsp-workspace-restart` (whose respawn is
# driven asynchronously by the old workspace's own buffer list via its
# process sentinel -- fragile here since the buffer already attached to the
# dying workspace has to be the same one lsp-mode decides to reconnect,
# and in practice a `textDocument/completion` request sent immediately
# after intermittently timed out against the not-yet-fully-reattached
# buffer), this explicitly `lsp-workspace-shutdown`s the workspace (no
# auto-restart attempted -- `lsp-restart` is set to `ignore` in
# emacs-test-init.el), closes the now-orphaned buffer, then opens a *fresh*
# buffer of the same fixture -- the exact same "no workspace yet for this
# root" connect path every other test in this suite already relies on, so
# it reliably re-sends `initialize` (with whatever
# `svlsp-test/initialization-options` currently holds) and `didOpen`
# together, in three steps:
#   1. set fuzzyCompletion=false, tear down + reconnect, confirm the
#      typo'd/non-contiguous prefix "wdth" (fuzzy-only match) no longer
#      offers WIDTH;
#   2. confirm a strict, exact-name prefix still works while disabled
#      (proves this is "no fuzzy", not "completion is broken entirely");
#   3. unconditionally reset the option to unset (default) and reconnect
#      again, confirming fuzzy matching is back -- this restores the shared
#      workspace to its default state for every test that runs after this
#      file, and runs as its own step regardless of whether steps 1-2
#      passed.
#
# Line-number reference (fixture, 1-based -> LSP 0-based), same as test_25:
#   WIDTH line 27 (0-based), char 19 — exact full-name prefix
#   wdth  line 28 (0-based), char 18 — typo'd/non-contiguous prefix

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/fuzzy_completion.sv"

section "configurable fuzzy-matching toggle (plan.md §6.11)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "fuzzyCompletion=false: workspace restarts and wdth no longer matches WIDTH" \
        "fuzzyCompletion=false: strict prefix WIDTH still matches" \
        "restore: resetting fuzzyCompletion restarts the workspace back to fuzzy-on"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    # --- disable fuzzy matching for the shared workspace, reconnect, probe -
    run_test "fuzzyCompletion=false: workspace restarts and wdth no longer matches WIDTH" \
        "(condition-case err
           (let* ((buf1 (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok1  (with-current-buffer buf1 (svlsp-test/wait-for-lsp 15)))
                  (ws   (with-current-buffer buf1 (car (lsp-workspaces)))))
             (when ws
               (lsp-workspace-shutdown ws)
               ;; lsp-workspace-shutdown only kills the process; the session's
               ;; folder->servers table (and thus whether a fresh (lsp) call
               ;; below spawns a new workspace vs. reattaches to this dying
               ;; one) isn't cleaned up until the process sentinel fires
               ;; asynchronously on process death -- wait for that here so
               ;; buf2 below can't race it.
               (let ((proc (lsp--workspace-proc ws)) (deadline (+ (float-time) 10)))
                 (while (and proc (process-live-p proc) (< (float-time) deadline))
                   (sit-for 0.2))))
             (svlsp-test/close-file buf1)
             (setq svlsp-test/initialization-options
                   (list :svlsp (list :fuzzyCompletion :json-false)))
             (let* ((buf2   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                    (ok2    (with-current-buffer buf2 (svlsp-test/wait-for-lsp 20)))
                    (result (when ok2
                              (with-current-buffer buf2
                                (lsp-request \"textDocument/completion\"
                                             (list :textDocument (list :uri (lsp--buffer-uri))
                                                   :position     (list :line 28 :character 18))))))
                    (items  (when (hash-table-p result) (gethash \"items\" result)))
                    (labels (when (listp items)
                              (mapcar (lambda (i) (gethash \"label\" i)) items))))
               (svlsp-test/close-file buf2)
               (if (and ok1 ws ok2 (not (member \"WIDTH\" labels))) t nil)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- exact/strict prefix still works with fuzzy matching disabled ------
    run_test "fuzzyCompletion=false: strict prefix WIDTH still matches" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 27 :character 19))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"WIDTH\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    # --- restore: unset the option and reconnect back to the default (fuzzy on)
    run_test "restore: resetting fuzzyCompletion restarts the workspace back to fuzzy-on" \
        "(condition-case err
           (let* ((buf1 (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok1  (with-current-buffer buf1 (svlsp-test/wait-for-lsp 15)))
                  (ws   (with-current-buffer buf1 (car (lsp-workspaces)))))
             (when ws
               (lsp-workspace-shutdown ws)
               ;; lsp-workspace-shutdown only kills the process; the session's
               ;; folder->servers table (and thus whether a fresh (lsp) call
               ;; below spawns a new workspace vs. reattaches to this dying
               ;; one) isn't cleaned up until the process sentinel fires
               ;; asynchronously on process death -- wait for that here so
               ;; buf2 below can't race it.
               (let ((proc (lsp--workspace-proc ws)) (deadline (+ (float-time) 10)))
                 (while (and proc (process-live-p proc) (< (float-time) deadline))
                   (sit-for 0.2))))
             (svlsp-test/close-file buf1)
             (setq svlsp-test/initialization-options nil)
             (let* ((buf2   (svlsp-test/open-file \"${SV_FIXTURE}\"))
                    (ok2    (with-current-buffer buf2 (svlsp-test/wait-for-lsp 20)))
                    (result (when ok2
                              (with-current-buffer buf2
                                (lsp-request \"textDocument/completion\"
                                             (list :textDocument (list :uri (lsp--buffer-uri))
                                                   :position     (list :line 28 :character 18))))))
                    (items  (when (hash-table-p result) (gethash \"items\" result)))
                    (labels (when (listp items)
                              (mapcar (lambda (i) (gethash \"label\" i)) items))))
               (svlsp-test/close-file buf2)
               (if (and ok1 ws ok2 (member \"WIDTH\" labels)) t nil)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
