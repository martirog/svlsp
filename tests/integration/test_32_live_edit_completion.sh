#!/usr/bin/env bash
# test_32_live_edit_completion.sh — verify dot-completion on text that is
# *typed into a live buffer*, not baked into the fixture's initial content.
#
# Every other completion test opens a file whose "// probe: ..." trigger
# text already exists on disk at a known (line, char) and just fires
# textDocument/completion at that fixed position. That never exercises the
# real editing path: didChange -> ChangeDebouncer (plan.md §6.8) -> a
# background recompile -> a fresh in-memory symbol table -> only then does
# completion see the new text at all. A completion bug specific to that
# path (e.g. a stale/uncompiled buffer) would not be caught by any
# existing test.
#
# This test instead inserts new text into an already-open buffer via real
# Emacs buffer edits (which lsp-mode turns into textDocument/didChange the
# same way a human typing would), waits past the 300ms debounce window,
# and *then* requests completion — computing the request position
# dynamically from point after the edit rather than a hand-counted
# constant. The insertion point itself is also found by search-forward on
# a literal "// INSERT-EDIT-HERE" marker rather than a hand-counted
# forward-line count, for the same reason: hand-counted line/char
# positions (into a fixture's header comment shifting them, in
# particular) have been a recurring, easy-to-make mistake in this test
# suite's earlier files.
#
# Fixture: live_edit_completion.sv — Widget (greet/wave) is declared on
# disk; `w` is declared on disk too. Test 1 types a *new usage* of the
# already-declared `w` that never existed in the file. Test 2 goes
# further: it types a *new declaration* (w2) and a new usage of it in the
# same edit, proving completion isn't relying on anything from the
# original on-disk parse for either the object or its usage.

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/live_edit_completion.sv"

section "dot-completion on freshly-typed, live-edited text (textDocument/completion)"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "a newly-typed usage of an already-declared object completes to its class member" \
        "the freshly-typed prefix still filters out a non-matching member" \
        "a variable declared AND used entirely within one live edit still completes"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
else
    run_test "a newly-typed usage of an already-declared object completes to its class member" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result
                   (when ok
                     (with-current-buffer buf
                       (goto-char (point-min))
                       (search-forward \"// INSERT-EDIT-HERE\")
                       (end-of-line)
                       (insert \"  // probe: w.gr\")
                       (sit-for 3) ;; past the 300ms debounce window
                       (let ((pos (list :line (1- (line-number-at-pos))
                                        :character (- (point) (line-beginning-position)))))
                         (lsp-request \"textDocument/completion\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     pos))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "the freshly-typed prefix still filters out a non-matching member" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result
                   (when ok
                     (with-current-buffer buf
                       (goto-char (point-min))
                       (search-forward \"// INSERT-EDIT-HERE\")
                       (end-of-line)
                       (insert \"  // probe: w.gr\")
                       (sit-for 3)
                       (let ((pos (list :line (1- (line-number-at-pos))
                                        :character (- (point) (line-beginning-position)))))
                         (lsp-request \"textDocument/completion\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     pos))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (not (member \"wave\" labels))) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "a variable declared AND used entirely within one live edit still completes" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${SV_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result
                   (when ok
                     (with-current-buffer buf
                       (goto-char (point-min))
                       (search-forward \"// INSERT-EDIT-HERE\")
                       (end-of-line)
                       (insert \"\\n  Widget w2;\\n  // probe: w2.gr\")
                       (sit-for 3)
                       (let ((pos (list :line (1- (line-number-at-pos))
                                        :character (- (point) (line-beginning-position)))))
                         (lsp-request \"textDocument/completion\"
                                      (list :textDocument (list :uri (lsp--buffer-uri))
                                            :position     pos))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (member \"greet\" labels)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
