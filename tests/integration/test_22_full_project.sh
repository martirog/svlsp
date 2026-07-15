#!/usr/bin/env bash
# test_22_full_project.sh — extensive end-to-end test combining every
# Phase 6 project-level feature in one fixture, checked against every
# position-based and name-based LSP feature:
#
#   - `include              (fp_top.sv includes fp_defs_inc.sv -> fp_sub_block)
#   - explicit project file  (fp_extra_mod.sv, listed in .svlsp.f, never
#                             `included or -y resolved -> fp_extra_mod)
#   - -y library resolution  (libs/fp_leaf_mod.sv, found only via
#                             `-y libs` + `+libext+.sv` -> fp_leaf_mod)
#   - wildcard import         (import fp_reexport_pkg::*; -> FpExtra)
#   - specific import         (import fp_util_pkg::fp_compute;)
#   - transitive export       (fp_reexport_pkg exports fp_util_pkg::*, so
#                             FpWidget is visible in fp_top.sv despite fp_top.sv
#                             never importing fp_util_pkg directly)
#   - macro expansion         (`define FP_BUS_WIDTH 16, used in fp_top.sv;
#                             proven indirectly via the zero-diagnostics
#                             check -- a broken expansion would be a parse
#                             error, per the mid-line macro gaps documented
#                             in test_19/test_20)
#
# Fixture: tests/integration/fixtures/full_project/
#   .svlsp.f            — "fp_top.sv" + "fp_extra_mod.sv" + "fp_util_pkg.sv"
#                         + "fp_reexport_pkg.sv" + "-y libs" + "+libext+.sv"
#                         (the two packages must be explicit project files --
#                         import/export resolution is a DB lookup by package
#                         name, with no library-resolution-style mechanism to
#                         go find a package's declaring file on demand the way
#                         module instantiations do; a real project's filelist
#                         lists every source file, packages included)
#   fp_top.sv           — `includes fp_defs_inc.sv; imports fp_reexport_pkg::*
#                         and fp_util_pkg::fp_compute; instantiates all three
#                         cross-file modules
#   fp_defs_inc.sv      — `define FP_BUS_WIDTH 16; module fp_sub_block
#   fp_extra_mod.sv     — module fp_extra_mod (explicit file, not include/-y)
#   libs/fp_leaf_mod.sv — module fp_leaf_mod (-y-resolved only)
#   fp_util_pkg.sv      — package fp_util_pkg { class FpWidget; function fp_compute }
#   fp_reexport_pkg.sv  — package fp_reexport_pkg { import/export fp_util_pkg::*;
#                         class FpExtra }
#
# All names are "fp_"-prefixed and unique across the whole fixtures tree
# (checked against every existing tests/integration/fixtures/*.sv and
# examples/*.sv symbol name) to avoid any cross-test collision in the
# single shared in-memory DB/server process the whole Emacs daemon session
# uses -- e.g. this fixture's library-resolved module is "fp_leaf_mod", NOT
# "leaf_mod" (already used by test_21's multifile_project fixture).
#
# Line-number reference (1-based -> LSP 0-based), computed programmatically
# rather than by hand to avoid off-by-one drift:
#   fp_top.sv:11           -> LSP line 10            — "module fp_top;"
#   fp_top.sv:9,  char 20  -> LSP line 8,  char 20    — "fp_compute" in the specific-import line
#   fp_top.sv:13, blank    -> LSP line 12, char 0     — completion trigger (inside fp_top, no prefix)
#   fp_top.sv:14, char 4   -> LSP line 13, char 4     — "FpExtra" reference
#   fp_top.sv:15, char 4   -> LSP line 14, char 4     — "FpWidget" reference
#   fp_top.sv:17, char 4   -> LSP line 16, char 4     — "fp_sub_block" instantiation
#   fp_top.sv:22, char 4   -> LSP line 21, char 4     — "fp_extra_mod" instantiation
#   fp_top.sv:24, char 4   -> LSP line 23, char 4     — "fp_leaf_mod" instantiation
#   fp_defs_inc.sv:4       -> LSP line 3, char 7      — "module fp_sub_block ("
#   fp_extra_mod.sv:4      -> LSP line 3, char 7      — "module fp_extra_mod ();"
#   libs/fp_leaf_mod.sv:3  -> LSP line 2, char 7      — "module fp_leaf_mod ();"
#   fp_util_pkg.sv:4       -> LSP line 3, char 10     — "class FpWidget;"
#   fp_util_pkg.sv:8       -> LSP line 7, char 27     — "function automatic int fp_compute(int x);"
#   fp_reexport_pkg.sv:8   -> LSP line 7, char 10     — "class FpExtra;"

FP_ROOT="${SVLSP_ROOT}/tests/integration/fixtures/full_project"
TOP_FIXTURE="${FP_ROOT}/fp_top.sv"
DEFS_INC_FIXTURE="${FP_ROOT}/fp_defs_inc.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "diagnostics: zero diagnostics for the full project" \
        "hover: fp_sub_block (via \`include) returns non-null" \
        "definition: fp_sub_block resolves into fp_defs_inc.sv" \
        "definition: fp_sub_block resolves to its declaration line" \
        "hover: fp_extra_mod (explicit project file) returns non-null" \
        "definition: fp_extra_mod resolves into fp_extra_mod.sv" \
        "definition: fp_extra_mod resolves to its declaration line" \
        "hover: fp_leaf_mod (-y library resolution) returns non-null" \
        "definition: fp_leaf_mod resolves into libs/fp_leaf_mod.sv" \
        "definition: fp_leaf_mod resolves to its declaration line" \
        "hover: FpExtra (wildcard import) returns non-null" \
        "definition: FpExtra resolves into fp_reexport_pkg.sv" \
        "hover: FpWidget (transitive export) returns non-null" \
        "definition: FpWidget resolves into fp_util_pkg.sv via export chain" \
        "hover: fp_compute (specific import) returns non-null" \
        "definition: fp_compute resolves into fp_util_pkg.sv" \
        "completion: includes FpExtra" \
        "completion: includes FpWidget" \
        "completion: includes fp_compute" \
        "completion: includes fp_sub_block" \
        "completion: includes fp_extra_mod" \
        "completion: includes fp_leaf_mod" \
        "documentSymbol: fp_top.sv has fp_top but not fp_sub_block" \
        "documentSymbol: fp_defs_inc.sv has fp_sub_block at LSP line 3" \
        "workspaceSymbol: FpWidget query points to fp_util_pkg.sv" \
        "workspaceSymbol: fp_leaf_mod query points to libs/fp_leaf_mod.sv"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "full project — diagnostics"
# ---------------------------------------------------------------------------

# Zero diagnostics proves the whole pipeline -- preprocessing/macro expansion,
# `include, imports, exports, explicit-file compilation, and -y library
# resolution -- came together without a single parse error.
run_test "diagnostics: zero diagnostics for the full project" \
    "(condition-case err
       (let* ((buf   (svlsp-test/open-file \"${TOP_FIXTURE}\"))
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
section "full project — \`include resolution (fp_sub_block)"
# ---------------------------------------------------------------------------

run_test "hover: fp_sub_block (via \`include) returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 16 :character 4)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: fp_sub_block resolves into fp_defs_inc.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 16 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"fp_defs_inc.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: fp_sub_block resolves to its declaration line" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 16 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 3)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "full project — explicit project file (fp_extra_mod)"
# ---------------------------------------------------------------------------

run_test "hover: fp_extra_mod (explicit project file) returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 21 :character 4)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: fp_extra_mod resolves into fp_extra_mod.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 21 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"fp_extra_mod.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: fp_extra_mod resolves to its declaration line" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 21 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 3)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "full project — -y library resolution (fp_leaf_mod)"
# ---------------------------------------------------------------------------

run_test "hover: fp_leaf_mod (-y library resolution) returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 23 :character 4)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: fp_leaf_mod resolves into libs/fp_leaf_mod.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 23 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"libs/fp_leaf_mod.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: fp_leaf_mod resolves to its declaration line" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 23 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\" (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 2)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "full project — wildcard import (FpExtra)"
# ---------------------------------------------------------------------------

run_test "hover: FpExtra (wildcard import) returns non-null" \
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

run_test "definition: FpExtra resolves into fp_reexport_pkg.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 13 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"fp_reexport_pkg.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "full project — transitive export (FpWidget via fp_reexport_pkg)"
# ---------------------------------------------------------------------------

# fp_top.sv never imports fp_util_pkg directly for FpWidget -- it's only
# reachable because fp_reexport_pkg re-exports fp_util_pkg::* and fp_top.sv
# imports fp_reexport_pkg::*. Proves the export chain works inside a full
# multi-file project, not just the isolated Phase 6.3 fixtures.
run_test "hover: FpWidget (transitive export) returns non-null" \
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

run_test "definition: FpWidget resolves into fp_util_pkg.sv via export chain" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 14 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"fp_util_pkg.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "full project — specific import (fp_compute)"
# ---------------------------------------------------------------------------

run_test "hover: fp_compute (specific import) returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 8 :character 20)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "definition: fp_compute resolves into fp_util_pkg.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 8 :character 20))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"fp_util_pkg.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "full project — completion (all six cross-file/import/export symbols)"
# ---------------------------------------------------------------------------

# Trigger at a blank line inside fp_top's body (LSP line 12, no prefix word)
# so every visible symbol is a completion candidate.
for item in FpExtra FpWidget fp_compute fp_sub_block fp_extra_mod fp_leaf_mod; do
    run_test "completion: includes ${item}" \
        "(condition-case err
           (let* ((buf     (svlsp-test/open-file \"${TOP_FIXTURE}\"))
                  (ok      (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result  (when ok
                             (with-current-buffer buf
                               (lsp-request \"textDocument/completion\"
                                            (list :textDocument (list :uri (lsp--buffer-uri))
                                                  :position     (list :line 12 :character 0))))))
                  (items   (when (hash-table-p result) (gethash \"items\" result)))
                  (has-it  (when (listp items)
                             (cl-some (lambda (it)
                                        (string= (gethash \"label\" it) \"${item}\"))
                                      items))))
             (svlsp-test/close-file buf)
             (if (and ok has-it) t nil))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "t"
done

# ---------------------------------------------------------------------------
section "full project — document symbols"
# ---------------------------------------------------------------------------

# fp_top.sv's own documentSymbol must contain fp_top but NOT fp_sub_block --
# fp_sub_block is attributed to fp_defs_inc.sv via the preprocessor source
# map (same invariant proven in test_15 for a single-file `include).
run_test "documentSymbol: fp_top.sv has fp_top but not fp_sub_block" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument (list :uri (lsp--buffer-uri)))))))
              (names   (when (listp result)
                         (mapcar (lambda (s) (gethash \"name\" s)) result)))
              (has-top (member \"fp_top\" names))
              (no-sub  (not (member \"fp_sub_block\" names))))
         (svlsp-test/close-file buf)
         (if (and ok has-top no-sub) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# fp_defs_inc.sv is never opened directly; its symbols reach the DB purely
# through fp_top.sv's `include compile. Queried by path only.
run_test "documentSymbol: fp_defs_inc.sv has fp_sub_block at LSP line 3" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/documentSymbol\"
                                       (list :textDocument
                                             (list :uri (lsp--path-to-uri \"${DEFS_INC_FIXTURE}\")))))))
              (sym    (when (listp result)
                        (cl-find \"fp_sub_block\" result
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
section "full project — workspace symbols"
# ---------------------------------------------------------------------------

run_test "workspaceSymbol: FpWidget query points to fp_util_pkg.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"workspace/symbol\"
                                       (list :query \"FpWidget\")))))
              (sym    (when (listp result)
                        (cl-find \"FpWidget\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (uri    (when sym (gethash \"uri\" (gethash \"location\" sym)))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"fp_util_pkg.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Proves workspace/symbol search reaches a file that was never opened by the
# editor and was only pulled in via -y library resolution.
run_test "workspaceSymbol: fp_leaf_mod query points to libs/fp_leaf_mod.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"workspace/symbol\"
                                       (list :query \"fp_leaf_mod\")))))
              (sym    (when (listp result)
                        (cl-find \"fp_leaf_mod\" result
                                 :key  (lambda (s) (gethash \"name\" s))
                                 :test #'string=)))
              (uri    (when sym (gethash \"uri\" (gethash \"location\" sym)))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"libs/fp_leaf_mod.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
