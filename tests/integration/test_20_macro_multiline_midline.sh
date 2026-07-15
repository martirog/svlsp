#!/usr/bin/env bash
# test_20_macro_multiline_midline.sh — verify a macro DEFINED across multiple
# physical lines via backslash-continuation, then INVOKED in the middle of a
# line (not at line start), preprocesses and parses correctly.
#
# `sv_preprocessor` reads and processes source strictly one physical line at
# a time (`std::getline` in `processSource`) and has NO handling anywhere for
# a trailing `\` continuation on a `` `define `` line. A continuation like:
#
#   `define WIDE_WIDTH \
#       16
#
# is legal SV (`` `define `` bodies may span multiple physical lines via
# backslash-continuation, same as the C preprocessor). Today the parser:
#   1. keeps the literal trailing backslash in the macro body instead of
#      merging the continuation line's content into it, and
#   2. emits the continuation line ("    16") as ordinary source code rather
#      than consuming it as part of the directive,
# both of which corrupt the expanded output and produce spurious parse
# errors — confirmed via a standalone probe of `SvPreprocessor::process` +
# `SvTreeWalker::walk` before writing this test.
#
# Fixture: tests/integration/fixtures/preproc_multiline_midline.sv
#   line  6: `define WIDE_WIDTH \            (start of definition)
#   line  7:     16                          (continuation — should be part of the macro body)
#   line  9: module multiline_midline_mod;
#   line 10:     wire [`WIDE_WIDTH-1:0] wide_bus;   (mid-line invocation, mirrors test_19)
#   line 11:     wire                   ready_out;  (control — no macro on this line)
#   line 13:     assign ready_out = |wide_bus;      (reference, for go-to-definition)
#
# Original-source columns (0-based, what the editor buffer actually contains):
#   wide_bus  declaration (line 10) -> column 27
#   ready_out declaration (line 11) -> column 27
#   ready_out reference    (line 13) -> column 11
#   wide_bus  reference    (line 13) -> column 24
#
# Line-number reference (1-based -> LSP 0-based):
#   preproc_multiline_midline.sv:9  -> LSP line 8  (module declaration)
#   preproc_multiline_midline.sv:10 -> LSP line 9  (wide_bus declaration)
#   preproc_multiline_midline.sv:11 -> LSP line 10 (ready_out declaration)
#   preproc_multiline_midline.sv:13 -> LSP line 12 (reference line)
#
# These assertions expect CORRECT behavior (valid SV, zero diagnostics, and
# wide_bus's column matching its true position in the original source). This
# test is expected to currently FAIL — both on the diagnostics count and on
# wide_bus's column — documenting a known gap (backslash-continuation is
# unsupported) on top of the mid-line column-drift gap from test_19. See
# handoff.md "Known gaps".

FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/preproc_multiline_midline.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "diagnostics: zero diagnostics for valid multi-line macro + mid-line invocation" \
        "documentSymbol: wide_bus selectionRange starts at original column 27" \
        "documentSymbol: ready_out (control) selectionRange starts at column 27" \
        "definition: wide_bus reference resolves to line 9" \
        "definition: wide_bus reference resolves to original column 27" \
        "hover: wide_bus reference returns non-null"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "multi-line macro definition, mid-line invocation — diagnostics"
# ---------------------------------------------------------------------------

# The fixture is valid SV. A macro whose definition spans two physical lines
# via backslash-continuation, then invoked mid-line, must not produce any
# parse errors.
run_test "diagnostics: zero diagnostics for valid multi-line macro + mid-line invocation" \
    "(condition-case err
       (let* ((buf   (svlsp-test/open-file \"${FIXTURE}\"))
              (ok    (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (count (when ok
                       (with-current-buffer buf
                         (sit-for 2)
                         (length (lsp--get-buffer-diagnostics))))))
         (svlsp-test/close-file buf)
         (if (and ok (eql count 0)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "multi-line macro definition, mid-line invocation — document symbols"
# ---------------------------------------------------------------------------

# wide_bus is declared after `WIDE_WIDTH on the same line. Its selectionRange
# must point at column 27 (its position in the original source), not wherever
# it lands in the corrupted/expanded internal text.
run_test "documentSymbol: wide_bus selectionRange starts at original column 27" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument (list :uri (lsp--buffer-uri)))))))
              (sym    (when (listp result)
                        (cl-find \"wide_bus\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (char   (when sym
                        (gethash \"character\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal char 27)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Control: ready_out has no macro on its declaration line, so its column must
# already be correct (27) regardless of the multi-line/mid-line macro bugs.
run_test "documentSymbol: ready_out (control) selectionRange starts at column 27" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument (list :uri (lsp--buffer-uri)))))))
              (sym    (when (listp result)
                        (cl-find \"ready_out\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (char   (when sym
                        (gethash \"character\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal char 27)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "multi-line macro definition, mid-line invocation — go-to-definition"
# ---------------------------------------------------------------------------

# Go to definition of "wide_bus" from its reference on line 13 (LSP line 12,
# character 24). Must resolve to LSP line 9, character 27 -- true position in
# the original source.
run_test "definition: wide_bus reference resolves to line 9" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 12 :character 24))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 9)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: wide_bus reference resolves to original column 27" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 12 :character 24))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (char   (when loc
                        (gethash \"character\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal char 27)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "multi-line macro definition, mid-line invocation — hover"
# ---------------------------------------------------------------------------

run_test "hover: wide_bus reference returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 12 :character 24)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
