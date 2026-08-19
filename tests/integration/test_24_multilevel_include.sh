#!/usr/bin/env bash
# test_24_multilevel_include.sh — verify LSP features work correctly across
# THREE levels of `include (test_15_preprocessor_lsp.sh only covers one),
# with preprocessor *statements* (not just plain symbols) -- macro
# definitions and `ifdef conditionals -- interacting across more than one
# include boundary.
#
# Fixture layout (tests/integration/fixtures/):
#   ml_top.sv    (level 1, opened directly) -- defines ML_TOP_WIDTH, then
#                `includes ml_level2.sv; ml_top_mod uses ML_TOP_WIDTH (local)
#                AND ML_LEVEL3_DEPTH (defined two include-levels down, in
#                ml_level3.sv -- proves a macro survives back up to the top
#                once the whole include chain unwinds); instantiates both
#                ml_level2_mod (1 level down) and ml_level3_mod (2 levels
#                down).
#   ml_level2.sv (level 2, middle) -- defines ML_LEVEL2_SCALE, then
#                `includes ml_level3.sv, then `ifdef ML_LEVEL3_FLAG gates
#                module ml_level2_mod -- the flag is defined INSIDE
#                ml_level3.sv, so this proves an `ifdef in the middle file
#                correctly sees a macro defined by the deepest file. Also has
#                a NEGATIVE `ifdef ML_NEVER_DEFINED guarding
#                ml_should_not_exist, which must never appear anywhere --
#                without a negative case, a test could pass even if `ifdef
#                were accidentally short-circuited to "always true".
#   ml_level3.sv (level 3, deepest) -- defines ML_LEVEL3_FLAG and
#                ML_LEVEL3_DEPTH, then module ml_level3_mod whose body uses
#                ML_TOP_WIDTH (defined in the TOP file, two include-levels
#                above -- proves downward visibility through more than one
#                nested include, the mirror image of the upward case in
#                ml_top.sv).
#
# Line-number reference (1-based -> LSP 0-based):
#   ml_top.sv:10    -> LSP line 9  -- "module ml_top_mod"
#   ml_top.sv:14    -> LSP line 13, char 4 -- "ml_level2_mod u_level2 ();"
#   ml_top.sv:15    -> LSP line 14, char 4 -- "ml_level3_mod u_level3 ();"
#   ml_level2.sv:9  -> LSP line 8  -- "module ml_level2_mod"
#   ml_level3.sv:11 -> LSP line 10 -- "module ml_level3_mod"

TOP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/ml_top.sv"
L2_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/ml_level2.sv"
L3_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/ml_level3.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "zero diagnostics after opening ml_top.sv" \
        "hover: ml_level2_mod instantiation returns non-null" \
        "hover: ml_level3_mod instantiation returns non-null" \
        "definition: ml_level2_mod resolves to ml_level2.sv at LSP line 8" \
        "definition: ml_level3_mod resolves to ml_level3.sv at LSP line 10" \
        "documentSymbol: ml_top.sv has ml_top_mod but not level2/level3/should_not_exist" \
        "documentSymbol: ml_level2.sv has ml_level2_mod at LSP line 8, not ml_should_not_exist" \
        "documentSymbol: ml_level3.sv has ml_level3_mod at LSP line 10" \
        "workspaceSymbol: ml_level3_mod query points to ml_level3.sv" \
        "workspaceSymbol: ml_should_not_exist has no match anywhere"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "multi-level include — diagnostics"
# ---------------------------------------------------------------------------

# Proves the whole 3-level chain -- both cross-boundary macro directions and
# the `ifdef spanning an include boundary -- compiles clean.
run_test "zero diagnostics after opening ml_top.sv" \
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
section "multi-level include — hover"
# ---------------------------------------------------------------------------

run_test "hover: ml_level2_mod instantiation returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 13 :character 4)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Proves cross-file resolution works at 2 levels of include distance, not
# just 1 -- findSymbolsByName is global/unscoped so this is expected to work
# with zero code changes, but nothing existing tested it.
run_test "hover: ml_level3_mod instantiation returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 14 :character 4)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "multi-level include — go-to-definition"
# ---------------------------------------------------------------------------

run_test "definition: ml_level2_mod resolves to ml_level2.sv at LSP line 8" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 13 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc)))
              (line   (when loc
                        (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"ml_level2.sv\" uri) (equal line 8)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# 2-level case: definition from ml_top.sv resolves directly into ml_level3.sv,
# skipping over the intermediate ml_level2.sv include boundary.
run_test "definition: ml_level3_mod resolves to ml_level3.sv at LSP line 10" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 14 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc)))
              (line   (when loc
                        (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"ml_level3.sv\" uri) (equal line 10)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "multi-level include — document symbols"
# ---------------------------------------------------------------------------

run_test "documentSymbol: ml_top.sv has ml_top_mod but not level2/level3/should_not_exist" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument (list :uri (lsp--buffer-uri)))))))
              (names  (when (listp result)
                        (mapcar (lambda (s) (gethash \"name\" s)) result)))
              (has-top (member \"ml_top_mod\" names))
              (no-l2   (not (member \"ml_level2_mod\" names)))
              (no-l3   (not (member \"ml_level3_mod\" names)))
              (no-nx   (not (member \"ml_should_not_exist\" names))))
         (svlsp-test/close-file buf)
         (if (and ok has-top no-l2 no-l3 no-nx) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ml_level2.sv queried by path (not opened as a buffer) -- same pattern as
# test_15's DEFS_FIXTURE check. Must contain ml_level2_mod at its correct
# original line, and must NOT contain ml_should_not_exist (negative `ifdef).
run_test "documentSymbol: ml_level2.sv has ml_level2_mod at LSP line 8, not ml_should_not_exist" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument
                                             (list :uri (lsp--path-to-uri \"${L2_FIXTURE}\")))))))
              (names  (when (listp result)
                        (mapcar (lambda (s) (gethash \"name\" s)) result)))
              (sym    (when (listp result)
                        (cl-find \"ml_level2_mod\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (line   (when sym
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym)))))
              (no-nx  (not (member \"ml_should_not_exist\" names))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 8) no-nx) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "documentSymbol: ml_level3.sv has ml_level3_mod at LSP line 10" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument
                                             (list :uri (lsp--path-to-uri \"${L3_FIXTURE}\")))))))
              (sym    (when (listp result)
                        (cl-find \"ml_level3_mod\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (line   (when sym
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"selectionRange\" sym))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 10)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "multi-level include — workspace symbols"
# ---------------------------------------------------------------------------

# Proves correct file attribution survives 2 levels of include nesting
# through the source map, matching the single-level assertion test_15 already
# makes for 1 level.
run_test "workspaceSymbol: ml_level3_mod query points to ml_level3.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"workspace/symbol\"
                                       (list :query \"ml_level3_mod\")))))
              (sym    (when (listp result)
                        (cl-find \"ml_level3_mod\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (uri    (when sym (gethash \"uri\" (gethash \"location\" sym)))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"ml_level3.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Negative `ifdef case, global check: ml_should_not_exist must never be
# indexed anywhere, no matter which file is searched from.
run_test "workspaceSymbol: ml_should_not_exist has no match anywhere" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"workspace/symbol\"
                                       (list :query \"ml_should_not_exist\")))))
              (sym    (when (listp result)
                        (cl-find \"ml_should_not_exist\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=))))
         (svlsp-test/close-file buf)
         (if (and ok (null sym)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
