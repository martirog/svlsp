#!/usr/bin/env bash
# test_16_class_scope_completion.sh — verify that completion is scoped to the
# class the cursor is in and does NOT bleed members from sibling classes.
#
# Fixture: two_classes.sv
#   class ClassA { int alpha; int beta; function method_a; }  lines 3–8
#   class ClassB { int gamma; int delta; function method_b; } lines 10–15
#
# Completion at a position inside ClassA (LSP line 3, char 0) must include
# ClassA members (alpha, beta, method_a) and must exclude ClassB members
# (gamma, delta, method_b).  The inverse must hold inside ClassB.
#
# Position reference (LSP 0-based lines):
#   line 3, char 0  — "    int alpha;"  inside ClassA, before method_a scope
#   line 10, char 0 — "    int gamma;"  inside ClassB, before method_b scope
# Both positions have an empty word prefix (leading space), so findSymbolsVisibleAt
# returns all symbols visible at that scope with no prefix filtering.

SV_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/two_classes.sv"

# Helper: extract labels from a completion result (hash-table with "items" list).
# Returns a space-separated string of label names for use with grep/member checks.
# Usage inside the Elisp expression only — this is just a comment for readability.

section "class scope completion"

if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "completion inside ClassA includes alpha (ClassA member)" \
        "completion inside ClassA includes beta (ClassA member)" \
        "completion inside ClassA includes method_a (ClassA method)" \
        "completion inside ClassA excludes gamma (ClassB member)" \
        "completion inside ClassA excludes delta (ClassB member)" \
        "completion inside ClassA excludes method_b (ClassB method)" \
        "completion inside ClassB includes gamma (ClassB member)" \
        "completion inside ClassB includes delta (ClassB member)" \
        "completion inside ClassB includes method_b (ClassB method)" \
        "completion inside ClassB excludes alpha (ClassA member)" \
        "completion inside ClassB excludes beta (ClassA member)" \
        "completion inside ClassB excludes method_a (ClassA method)"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
# ClassA scope — line 3 (0-based), char 0
# scopeAtPosition for 1-based line 4 returns "ClassA"; method_a starts at
# line 6 so is not yet in scope.  findSymbolsVisibleAt returns:
#   scope="ClassA"  → alpha, beta, method_a
#   scope=""        → ClassA, ClassB
# ---------------------------------------------------------------------------

run_test "completion inside ClassA includes alpha (ClassA member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 3 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"alpha\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassA includes beta (ClassA member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 3 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"beta\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassA includes method_a (ClassA method)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 3 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"method_a\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassA excludes gamma (ClassB member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 3 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"gamma\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassA excludes delta (ClassB member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 3 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"delta\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassA excludes method_b (ClassB method)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 3 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"method_b\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
# ClassB scope — line 10 (0-based), char 0
# scopeAtPosition for 1-based line 11 returns "ClassB"; method_b starts at
# line 13 so is not yet in scope.  findSymbolsVisibleAt returns:
#   scope="ClassB"  → gamma, delta, method_b
#   scope=""        → ClassA, ClassB
# ---------------------------------------------------------------------------

run_test "completion inside ClassB includes gamma (ClassB member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"gamma\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassB includes delta (ClassB member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"delta\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassB includes method_b (ClassB method)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (member \"method_b\" labels)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassB excludes alpha (ClassA member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"alpha\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassB excludes beta (ClassA member)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"beta\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "completion inside ClassB excludes method_a (ClassA method)" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${SV_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/completion\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 10 :character 0))))))
              (items  (when (hash-table-p result) (gethash \"items\" result)))
              (labels (when (listp items)
                        (mapcar (lambda (i) (gethash \"label\" i)) items))))
         (svlsp-test/close-file buf)
         (if (and ok (not (member \"method_a\" labels))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
