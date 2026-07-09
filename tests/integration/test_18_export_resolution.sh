#!/usr/bin/env bash
# test_18_export_resolution.sh — verify that `export pkg::*;` re-exports a
# package's imported scope transitively, while a plain (non-exported) import
# of that same package stays scoped to only the immediately imported package.
#
# Fixtures:
#   base_pkg.sv           — package base_pkg { class Alpha }
#   middle_pkg.sv         — package middle_pkg { import base_pkg::*; export base_pkg::*;
#                                                 class Beta }
#   export_user.sv        — import middle_pkg::*; module export_user
#   plain_middle_pkg.sv   — package plain_middle_pkg { import base_pkg::*; class Gamma }
#                            (no export — re-export must NOT leak)
#   plain_import_user.sv  — import plain_middle_pkg::*; module plain_import_user
#
# base_pkg.sv (1-based line → LSP 0-based):
#   line 3 → LSP 2, char 10 — "    class Alpha;"
#
# middle_pkg.sv (1-based line → LSP 0-based):
#   line 6 → LSP 5, char 10 — "    class Beta;"
#
# export_user.sv (1-based line → LSP 0-based):
#   line 8  → LSP 7, char 4  — "    Beta  b;"    word = "Beta"
#   line 9  → LSP 8, char 4  — "    Alpha a;"    word = "Alpha" (visible only via export)
#   line 10 → LSP 9, char 0  — completion trigger
#
# plain_import_user.sv (1-based line → LSP 0-based):
#   line 8 → LSP 7, char 0 — completion trigger (Alpha must be absent here)

BASE_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/base_pkg.sv"
MIDDLE_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/middle_pkg.sv"
EXPORT_USER_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/export_user.sv"
PLAIN_MIDDLE_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/plain_middle_pkg.sv"
PLAIN_USER_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/plain_import_user.sv"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "prerequisite: base_pkg.sv symbols seeded in DB" \
        "prerequisite: middle_pkg.sv symbols + import/export seeded in DB" \
        "prerequisite: plain_middle_pkg.sv symbols + import seeded in DB" \
        "export completion: includes Beta (middle_pkg's own symbol)" \
        "export completion: includes Alpha (re-exported from base_pkg)" \
        "export hover: Alpha returns non-null" \
        "export definition: Alpha resolves to base_pkg.sv" \
        "export definition: Alpha at LSP line 2" \
        "plain completion: includes Gamma (plain_middle_pkg's own symbol)" \
        "plain completion: excludes Alpha (base_pkg not exported by plain_middle_pkg)"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "export resolution — prerequisites"
# ---------------------------------------------------------------------------

run_test "prerequisite: base_pkg.sv symbols seeded in DB" \
    "(condition-case err
       (let* ((buf (svlsp-test/open-file \"${BASE_FIXTURE}\"))
              (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
         (svlsp-test/close-file buf)
         (if ok t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "prerequisite: middle_pkg.sv symbols + import/export seeded in DB" \
    "(condition-case err
       (let* ((buf (svlsp-test/open-file \"${MIDDLE_FIXTURE}\"))
              (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
         (svlsp-test/close-file buf)
         (if ok t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "prerequisite: plain_middle_pkg.sv symbols + import seeded in DB" \
    "(condition-case err
       (let* ((buf (svlsp-test/open-file \"${PLAIN_MIDDLE_FIXTURE}\"))
              (ok  (with-current-buffer buf (svlsp-test/wait-for-lsp 15))))
         (svlsp-test/close-file buf)
         (if ok t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "export resolution — export base_pkg::* re-exports transitively"
# ---------------------------------------------------------------------------

run_test "export completion: includes Beta (middle_pkg's own symbol)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${EXPORT_USER_FIXTURE}\"))
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
         (if (and ok (member \"Beta\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "export completion: includes Alpha (re-exported from base_pkg)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${EXPORT_USER_FIXTURE}\"))
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
         (if (and ok (member \"Alpha\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Hover on "Alpha" at line 8 (0-based) = "    Alpha a;" char 4.
# findSymbolsByName("Alpha") must find it in base_pkg.sv via the DB.
run_test "export hover: Alpha returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${EXPORT_USER_FIXTURE}\"))
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

# Go-to-definition on "Alpha" in export_user.sv must route to base_pkg.sv —
# confirming the resolved symbol comes from the correctly re-exported scope,
# not merely that some symbol happens to be visible.
run_test "export definition: Alpha resolves to base_pkg.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${EXPORT_USER_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 8 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"base_pkg.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Definition must point to LSP line 2 (0-based = 1-based line 3 = "    class Alpha;").
run_test "export definition: Alpha at LSP line 2" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${EXPORT_USER_FIXTURE}\"))
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
         (if (and ok (equal line 2)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "export resolution — plain import stays non-transitive (regression guard)"
# ---------------------------------------------------------------------------

# Completion at line 8 (0-based) inside plain_import_user, which only imports
# plain_middle_pkg::* (no export declared anywhere). Gamma (plain_middle_pkg's
# own symbol) must be visible; Alpha (base_pkg, two levels away) must NOT be.

run_test "plain completion: includes Gamma (plain_middle_pkg's own symbol)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${PLAIN_USER_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 8 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"Gamma\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "plain completion: excludes Alpha (base_pkg not exported by plain_middle_pkg)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${PLAIN_USER_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 8 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"Alpha\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
