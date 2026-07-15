#!/usr/bin/env bash
# test_19_macro_midline_expansion.sh — verify that symbol columns are correct
# when a macro invocation appears in the MIDDLE of a line (not at line start)
# and expands to text of a different length than the invocation itself.
#
# `sv_preprocessor`'s source map only translates LINE numbers across macro
# expansion; it does not track column drift caused by a macro shrinking or
# growing the text earlier on the same line. Any symbol declared after such a
# macro on the same line therefore risks having a column that matches the
# internal *expanded* text rather than the original buffer the editor (and
# the LSP client) actually has open.
#
# Fixture: tests/integration/fixtures/preproc_midline.sv
#   line 5: `define WIDTH 8
#   line 8: `    wire [\`WIDTH-1:0] data_bus;`
#     - "`WIDTH" (6 chars) expands to "8" (1 char) -> 5-column shrink
#     - "data_bus" sits at column 22 in the ORIGINAL source (what the editor
#       buffer contains) but column 17 in the internally expanded text
#   line 9: `    wire              valid_out;`  (no macro on this line; control)
#     - "valid_out" sits at column 22 in both original and expanded text
#   line 11: `    assign valid_out = |data_bus;`  (reference, for go-to-definition)
#     - "valid_out" at column 11, "data_bus" at column 24
#
# Line-number reference (1-based -> LSP 0-based):
#   preproc_midline.sv:8  -> LSP line 7  (data_bus declaration)
#   preproc_midline.sv:9  -> LSP line 8  (valid_out declaration)
#   preproc_midline.sv:11 -> LSP line 10 (reference line)
#
# These assertions expect the CORRECT (original-source) column. This test is
# expected to currently FAIL for data_bus, documenting a known column-drift
# bug to be fixed separately (see handoff.md "Known gaps").

FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/preproc_midline.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "documentSymbol: data_bus selectionRange starts at original column 22" \
        "documentSymbol: valid_out (no macro on its line) selectionRange starts at column 22" \
        "definition: data_bus reference resolves to line 7" \
        "definition: data_bus reference resolves to original column 22" \
        "hover: data_bus reference returns non-null"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "mid-line macro expansion — document symbols"
# ---------------------------------------------------------------------------

# data_bus is declared after `WIDTH on the same line. Its selectionRange must
# point at column 22 (its position in the original source), not column 17
# (its position in the macro-expanded text used internally for parsing).
run_test "documentSymbol: data_bus selectionRange starts at original column 22" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument (list :uri (lsp--buffer-uri)))))))
              (sym    (when (listp result)
                        (cl-find \"data_bus\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (char   (when sym
                        (gethash \"character\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal char 22)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Control: valid_out has no macro on its declaration line, so its column must
# already be correct (22) regardless of the mid-line expansion bug.
run_test "documentSymbol: valid_out (no macro on its line) selectionRange starts at column 22" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument (list :uri (lsp--buffer-uri)))))))
              (sym    (when (listp result)
                        (cl-find \"valid_out\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (char   (when sym
                        (gethash \"character\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal char 22)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "mid-line macro expansion — go-to-definition"
# ---------------------------------------------------------------------------

# Go to definition of "data_bus" from its reference on line 11 (LSP line 10,
# character 24). Must resolve to LSP line 7, character 22 -- the symbol's true
# position in the original source, not the macro-expanded internal text.
run_test "definition: data_bus reference resolves to line 7" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 24))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 7)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: data_bus reference resolves to original column 22" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 24))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (char   (when loc
                        (gethash \"character\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal char 22)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "mid-line macro expansion — hover"
# ---------------------------------------------------------------------------

# Hover looks symbols up by name (not column), so it should be unaffected by
# the column-drift bug -- sanity check that the feature still works at all
# once a macro sits mid-line before the referenced symbol.
run_test "hover: data_bus reference returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 24)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
