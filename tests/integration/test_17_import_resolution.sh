#!/usr/bin/env bash
# test_17_import_resolution.sh — verify that package import resolution surfaces
# the correct symbols for wildcard (import pkg::*) and specific (import pkg::Foo)
# imports via the Phase 6.3 imports DB table.
#
# Fixtures:
#   util_pkg.sv         — package util_pkg { class DataItem; class Logger; function compute }
#   import_wildcard.sv  — import util_pkg::*; module wildcard_user
#   import_specific.sv  — import util_pkg::DataItem; module specific_user
#   import_same_file.sv — packages sfwi_pkg/sfwi_other_pkg declared in the same
#                         file as `import sfwi_pkg::*;` and module sfwi_top
#                         (plan.md §6.30 step 1)
#
# util_pkg.sv (1-based line → LSP 0-based):
#   line 3 → LSP 2  — "    class DataItem;"  (char 10 = 'D')
#   line 9 → LSP 8  — "    class Logger;"    (char 10 = 'L')
#   line 13→ LSP 12 — "    function automatic int compute(int x);"
#
# import_wildcard.sv (1-based line → LSP 0-based):
#   line 8 → LSP 7, char 4  — "    DataItem item;"      word = "DataItem"
#   line 9 → LSP 8, char 4  — "    Logger   log_obj;"   word = "Logger"
#   line 10→ LSP 9, char 0  — "    // LSP line 9..."    word = "" (completion trigger)
#
# import_specific.sv (1-based line → LSP 0-based):
#   line 7 → LSP 6, char 0  — "    // LSP line 5..."    word = "" (completion trigger)

PKG_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/util_pkg.sv"
WILDCARD_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/import_wildcard.sv"
SPECIFIC_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/import_specific.sv"
SAME_FILE_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/import_same_file.sv"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "prerequisite: util_pkg.sv symbols seeded in DB" \
        "wildcard completion: includes DataItem" \
        "wildcard completion: includes Logger" \
        "wildcard completion: includes compute" \
        "wildcard hover: DataItem returns non-null" \
        "wildcard definition: DataItem resolves to util_pkg.sv" \
        "wildcard definition: DataItem at LSP line 2" \
        "specific completion: includes DataItem (specifically imported)" \
        "specific completion: excludes Logger (not imported)" \
        "specific completion: excludes compute (not imported)" \
        "same-file wildcard completion: includes the imported package's class" \
        "same-file wildcard completion: excludes a non-imported package's class"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "package import resolution — prerequisite"
# ---------------------------------------------------------------------------

# Open util_pkg.sv to compile it and seed its symbols into the DB.
# All subsequent tests in this file depend on DataItem, Logger, and compute
# being present in the symbols table under scope="util_pkg".
run_test "prerequisite: util_pkg.sv symbols seeded in DB" \
    "(condition-case err
       (let* ((buf (svlsp-test/open-file \"${PKG_FIXTURE}\"))
              (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
         (svlsp-test/close-file buf)
         (if ok t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "package import resolution — wildcard import (import util_pkg::*)"
# ---------------------------------------------------------------------------

# Completion at line 9 (0-based) = "    // LSP line 9..." inside wildcard_user.
# wildcard import makes ALL util_pkg-scoped symbols visible via Part 2 of
# findSymbolsVisibleAt (scope IN ('', 'util_pkg')).

run_test "wildcard completion: includes DataItem" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${WILDCARD_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 9 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"DataItem\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "wildcard completion: includes Logger" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${WILDCARD_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 9 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"Logger\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "wildcard completion: includes compute" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${WILDCARD_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 9 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"compute\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Hover on "DataItem" at line 7 (0-based) = "    DataItem item;" char 4.
# findSymbolsByName("DataItem") must find it in util_pkg.sv via the DB.
run_test "wildcard hover: DataItem returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${WILDCARD_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 7 :character 4)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Go-to-definition on "DataItem" in import_wildcard.sv must route to util_pkg.sv.
run_test "wildcard definition: DataItem resolves to util_pkg.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${WILDCARD_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 7 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"util_pkg.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Definition must point to LSP line 2 (0-based = 1-based line 3 = "    class DataItem;").
run_test "wildcard definition: DataItem at LSP line 2" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${WILDCARD_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 7 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 2)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "package import resolution — specific import (import util_pkg::DataItem)"
# ---------------------------------------------------------------------------

# Completion at line 6 (0-based) = "    // LSP line 5..." inside specific_user.
# Only DataItem is imported; Logger and compute must NOT appear (Part 3 filter).

run_test "specific completion: includes DataItem (specifically imported)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SPECIFIC_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 6 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"DataItem\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "specific completion: excludes Logger (not imported)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SPECIFIC_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 6 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"Logger\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "specific completion: excludes compute (not imported)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SPECIFIC_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 6 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"compute\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "package import resolution — wildcard import of a same-file package"
# ---------------------------------------------------------------------------

# Completion at line 16 (0-based), char 4, inside sfwi_top. Before plan.md
# §6.30 step 1, findSymbolsVisibleAt excluded the cursor's own file from the
# wildcard-package arm, so a package declared in the same file was invisible.

same_file_completion_has() {
    run_test "$1" \
        "(condition-case err
           (let* ((buf    (svlsp-test/open-file \"${SAME_FILE_FIXTURE}\"))
                  (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
                  (result (when ok
                            (with-current-buffer buf
                              (lsp-request \"textDocument/completion\"
                                           (list :textDocument (list :uri (lsp--buffer-uri))
                                                 :position     (list :line 16 :character 4))))))
                  (items  (when (hash-table-p result) (gethash \"items\" result)))
                  (labels (when (listp items)
                            (mapcar (lambda (i) (gethash \"label\" i)) items))))
             (svlsp-test/close-file buf)
             (if (and ok (if (member \"$2\" labels) t nil)) \"present\" \"absent\"))
         (error (format \"elisp-error: %s\" (error-message-string err))))" \
        "\"$3\""
}

same_file_completion_has "same-file wildcard completion: includes the imported package's class" \
    sfwi_Item present
same_file_completion_has "same-file wildcard completion: excludes a non-imported package's class" \
    sfwi_Other absent
