#!/usr/bin/env bash
# test_37_diagnostics_visibility.sh — verify the "LSP diagnostics-visibility
# gap" fix: diagnostics for a transitively `` `include ``d file are published
# to the client under that file's own URI, not just silently computed and
# persisted to the DB. Before this fix, LanguageServer::compileAndPublish
# only ever published the primary (`didOpen`ed) file's own diagnostics.
#
# Fixture layout:
#   diagvis_top.sv — clean; `include`s diagvis_inc.sv
#   diagvis_inc.sv — intentionally broken; never opened directly by this
#                    test, so its diagnostics can only reach the client via
#                    the server's own push, keyed by its own URI.
#
# `(lsp-diagnostics)` returns a session-wide hash keyed by file path
# (`lsp--fix-path-casing` + `lsp--uri-to-path` of whatever URI a
# publishDiagnostics notification carried), independent of whether that
# path has ever had a buffer opened for it -- exactly the mechanism this
# fix relies on the client already supporting (see handoff.md's research
# note: this is a normal, spec-supported part of LSP, not a limitation).

TOP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/diagvis_top.sv"
INC_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/diagvis_inc.sv"

section "LSP diagnostics-visibility gap — included file diagnostics reach the client"

if [ ! -x "${SVLSP_BIN}" ]; then
    skip_test "included file's own diagnostics appear in (lsp-diagnostics) though it was never opened" \
        "svlsp binary not found at ${SVLSP_BIN}"
    skip_test "the primary file's own diagnostics stay clean (unaffected by the included file's error)" \
        "svlsp binary not found at ${SVLSP_BIN}"
else
    run_test "included file's own diagnostics appear in (lsp-diagnostics) though it was never opened" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
             (when ok (with-current-buffer buf (sit-for 2)))
             (let* ((inc-key (lsp--fix-path-casing (expand-file-name \"${INC_FIXTURE}\")))
                    (diags   (gethash inc-key (lsp-diagnostics))))
               (svlsp-test/close-file buf)
               (if (and ok diags (> (length diags) 0)) t nil)))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"

    run_test "the primary file's own diagnostics stay clean (unaffected by the included file's error)" \
        "(condition-case err
           (let* ((buf (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (n   (when ok (with-current-buffer buf
                                  (sit-for 2)
                                  (length (lsp--get-buffer-diagnostics))))))
             (svlsp-test/close-file buf)
             (if (and ok (eql n 0)) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
fi
