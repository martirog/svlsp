#!/usr/bin/env bash
# test_23_include_file_line.sh — verify that `__FILE__`/`__LINE__` inside an
# `include`d file are resolved (not left as literal, undefined macro
# invocations).
#
# Regression test for "Gap B" (see handoff.md): CompilerDirectiveStripper
# (pass 1, substitutes `__FILE__`/`__LINE__` with literals) used to run only
# once, on the top-level source, before SvPreprocessor::process (pass 2)
# began. `include`d files were read as raw text by processInclude and never
# passed through pass 1, so any `__FILE__`/`__LINE__` inside an included file
# reached pass 2 as a literal, still-unresolved macro invocation -> an
# "undefined macro" diagnostic. Discovered running the real UVM corpus
# (9x `__FILE__`, 8x `__LINE__`, all inside included .svh files).
#
# Fixture layout:
#   gapb_top.sv — `includes gapb_inc.sv; defines gapb_top_mod (line 5)
#   gapb_inc.sv — gapb_before_mod (line 5), gapb_marker_mod (line 8, uses
#                 `__FILE__`/`__LINE__` in its parameter defaults),
#                 gapb_after_mod (line 13)
#
# gapb_before_mod/gapb_after_mod straddle the `__FILE__`/`__LINE__` usage so
# a broken line count from pass-1 stripping (which must replace stripped
# lines with blanks, not delete them) would misplace gapb_after_mod.
#
# Line-number reference (1-based -> LSP 0-based):
#   gapb_top.sv:5   -> LSP line 4  — "module gapb_top_mod"
#   gapb_inc.sv:5   -> LSP line 4  — "module gapb_before_mod"
#   gapb_inc.sv:8   -> LSP line 7  — "module gapb_marker_mod"
#   gapb_inc.sv:13  -> LSP line 12 — "module gapb_after_mod"

TOP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/gapb_top.sv"
INC_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/gapb_inc.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "zero diagnostics after opening gapb_top.sv" \
        "documentSymbol: gapb_top.sv has gapb_top_mod but not gapb_before_mod/gapb_after_mod" \
        "documentSymbol: gapb_inc.sv has gapb_before_mod at LSP line 4" \
        "documentSymbol: gapb_inc.sv has gapb_marker_mod at LSP line 7" \
        "documentSymbol: gapb_inc.sv has gapb_after_mod at LSP line 12" \
        "workspaceSymbol: gapb_marker_mod query points to gapb_inc.sv"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "Gap B — __FILE__/__LINE__ inside included files — diagnostics"
# ---------------------------------------------------------------------------

# Primary regression check: before the fix, `__FILE__`/`__LINE__` inside
# gapb_inc.sv reached pass 2 unresolved and produced "undefined macro"
# diagnostics on gapb_top.sv (routed there since the source map only
# attributes lines it recognizes).
run_test "zero diagnostics after opening gapb_top.sv" \
    "(condition-case err
       (let* ((buf (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok  (with-current-buffer buf
                     (svlsp-test/wait-for-lsp 15)))
              (count
                (when ok
                  (with-current-buffer buf
                    (sit-for 2)
                    (length (lsp--get-buffer-diagnostics))))))
         (svlsp-test/close-file buf)
         (if (and ok (eql count 0)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "Gap B — __FILE__/__LINE__ inside included files — document symbols"
# ---------------------------------------------------------------------------

# gapb_top.sv's own symbols must be limited to gapb_top_mod; the included
# file's modules are routed to gapb_inc.sv's file_id by the source map.
run_test "documentSymbol: gapb_top.sv has gapb_top_mod but not gapb_before_mod/gapb_after_mod" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument (list :uri (lsp--buffer-uri)))))))
              (names   (when (listp result)
                         (mapcar (lambda (s) (gethash \"name\" s)) result)))
              (has-top (member \"gapb_top_mod\" names))
              (no-before (not (member \"gapb_before_mod\" names)))
              (no-after  (not (member \"gapb_after_mod\" names))))
         (svlsp-test/close-file buf)
         (if (and ok has-top no-before no-after) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# gapb_inc.sv (queried by path, not opened as a buffer) must contain all
# three of its modules at their correct original lines -- proving pass-1
# stripping of `__FILE__`/`__LINE__` inside the included file preserved its
# line count and did not corrupt parsing of what follows.
run_test "documentSymbol: gapb_inc.sv has gapb_before_mod at LSP line 4" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument
                                             (list :uri (lsp--path-to-uri \"${INC_FIXTURE}\")))))))
              (sym    (when (listp result)
                        (cl-find \"gapb_before_mod\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (line   (when sym
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 4)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "documentSymbol: gapb_inc.sv has gapb_marker_mod at LSP line 7" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument
                                             (list :uri (lsp--path-to-uri \"${INC_FIXTURE}\")))))))
              (sym    (when (listp result)
                        (cl-find \"gapb_marker_mod\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (line   (when sym
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 7)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "documentSymbol: gapb_inc.sv has gapb_after_mod at LSP line 12" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument
                                             (list :uri (lsp--path-to-uri \"${INC_FIXTURE}\")))))))
              (sym    (when (listp result)
                        (cl-find \"gapb_after_mod\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (line   (when sym
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 12)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "Gap B — __FILE__/__LINE__ inside included files — workspace symbols"
# ---------------------------------------------------------------------------

# workspace/symbol for gapb_marker_mod (the module whose parameters actually
# use `__FILE__`/`__LINE__`) must point to gapb_inc.sv, confirming correct
# file attribution survives the marker module's own successful parse.
run_test "workspaceSymbol: gapb_marker_mod query points to gapb_inc.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"workspace/symbol\"
                                       (list :query \"gapb_marker_mod\")))))
              (sym    (when (listp result)
                        (cl-find \"gapb_marker_mod\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (uri    (when sym (gethash \"uri\" (gethash \"location\" sym)))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"gapb_inc.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
