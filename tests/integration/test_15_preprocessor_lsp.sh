#!/usr/bin/env bash
# test_15_preprocessor_lsp.sh — verify that LSP features report correct
# file paths and line numbers when preprocessor directives shift line numbers.
#
# Fixture layout:
#   preproc_main.sv   — `includes preproc_defs.sv; defines top_wrapper (line 4)
#   preproc_defs.sv   — defines sub_block (line 4) and `define BUS_W 8
#
# After the server compiles preproc_main.sv the source map must route
# sub_block to preproc_defs.sv at its original line (1-based: 4, LSP: 3),
# NOT to preproc_main.sv.  Each LSP feature is tested against both files.
#
# Line-number reference (1-based → LSP 0-based):
#   preproc_defs.sv:4  → LSP line 3  — "module sub_block"
#   preproc_main.sv:4  → LSP line 3  — "module top_wrapper"
#   preproc_main.sv:9  → LSP line 8  — "    sub_block u_sub ("  (char 4)
#   preproc_main.sv:13 → LSP line 12 — "    );"  (char 0 — inside body, no word)

MAIN_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/preproc_main.sv"
DEFS_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/preproc_defs.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "hover: sub_block in main file returns non-null" \
        "hover: content mentions sub_block" \
        "definition: sub_block resolves to preproc_defs.sv" \
        "definition: sub_block line is 3 (original line 4 in defs)" \
        "definition: top_wrapper stays in preproc_main.sv" \
        "completion: inside top_wrapper includes sub_block from included file" \
        "documentSymbol: preproc_main.sv has top_wrapper but not sub_block" \
        "documentSymbol: preproc_defs.sv has sub_block at LSP line 3" \
        "workspaceSymbol: sub_block query points to preproc_defs.sv"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "preprocessor source-map — hover"
# ---------------------------------------------------------------------------

# Hover on "sub_block" at line 8 (0-based), char 4 in preproc_main.sv.
# sub_block is defined in preproc_defs.sv; source map must attribute it there.
# findSymbolsByName("sub_block") should still find it and return non-null hover.
run_test "hover: sub_block in main file returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 8 :character 4)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Check that the hover markdown mentions "sub_block".
run_test "hover: content mentions sub_block" \
    "(condition-case err
       (let* ((buf     (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok      (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result  (when ok
                         (with-current-buffer buf
                           (lsp-request \"textDocument/hover\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 8 :character 4))))))
              (content (when result
                         (let ((c (gethash \"contents\" result)))
                           (when (hash-table-p c) (gethash \"value\" c))))))
         (svlsp-test/close-file buf)
         (if (and ok content (string-match-p \"sub_block\" content)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "preprocessor source-map — go-to-definition"
# ---------------------------------------------------------------------------

# Definition of "sub_block" from preproc_main.sv must point to preproc_defs.sv.
run_test "definition: sub_block resolves to preproc_defs.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 8 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"preproc_defs.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Definition must report line 3 (0-based) — i.e. original line 4 in the defs
# file, before the include expanded the line numbers.
run_test "definition: sub_block line is 3 (original line 4 in defs)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 8 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 3)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Sanity: definition of top_wrapper (defined in the main file itself) must
# remain in preproc_main.sv, not drift into the included file.
run_test "definition: top_wrapper stays in preproc_main.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 3 :character 9))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"preproc_main.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "preprocessor source-map — completion"
# ---------------------------------------------------------------------------

# Completion at line 12 (0-based) = \"    );\" inside top_wrapper body.
# No prefix word (cursor on whitespace), so all visible symbols are candidates.
# sub_block has scope=\"\" in preproc_defs.sv, so it appears as a cross-file
# top-level symbol in findSymbolsVisibleAt and must be in the completion list.
run_test "completion: inside top_wrapper includes sub_block from included file" \
    "(condition-case err
       (let* ((buf     (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok      (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result  (when ok
                         (with-current-buffer buf
                           (lsp-request \"textDocument/completion\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 12 :character 0))))))
              (items   (when (hash-table-p result) (gethash \"items\" result)))
              (has-sub (when (listp items)
                         (cl-some (lambda (item)
                                    (string= (gethash \"label\" item) \"sub_block\"))
                                  items))))
         (svlsp-test/close-file buf)
         (if (and ok has-sub) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "preprocessor source-map — document symbols"
# ---------------------------------------------------------------------------

# documentSymbol for preproc_main.sv must contain top_wrapper but NOT sub_block.
# sub_block was routed to preproc_defs.sv's file_id by the source map;
# preproc_main.sv's symbols are only those defined in that file directly.
run_test "documentSymbol: preproc_main.sv has top_wrapper but not sub_block" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument (list :uri (lsp--buffer-uri)))))))
              (names   (when (listp result)
                         (mapcar (lambda (s) (gethash \"name\" s)) result)))
              (has-top (member \"top_wrapper\" names))
              (no-sub  (not (member \"sub_block\" names))))
         (svlsp-test/close-file buf)
         (if (and ok has-top no-sub) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# documentSymbol for preproc_defs.sv (not opened; symbols are in DB from
# compiling preproc_main.sv).  sub_block must appear at LSP line 3.
run_test "documentSymbol: preproc_defs.sv has sub_block at LSP line 3" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument
                                             (list :uri (lsp--path-to-uri \"${DEFS_FIXTURE}\")))))))
              (sym    (when (listp result)
                        (cl-find \"sub_block\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (line   (when sym
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 3)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "preprocessor source-map — workspace symbols"
# ---------------------------------------------------------------------------

# workspace/symbol query "sub_block" must return a result whose location URI
# points to preproc_defs.sv, not preproc_main.sv.
run_test "workspaceSymbol: sub_block query points to preproc_defs.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${MAIN_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"workspace/symbol\"
                                       (list :query \"sub_block\")))))
              (sym    (when (listp result)
                        (cl-find \"sub_block\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (uri    (when sym (gethash \"uri\" (gethash \"location\" sym)))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"preproc_defs.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
