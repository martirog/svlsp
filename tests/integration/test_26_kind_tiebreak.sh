#!/usr/bin/env bash
# test_26_kind_tiebreak.sh — end-to-end proof that hover/definition's
# declaration-like-kind tiebreak (pickBestSymbol(), src/lsp/symbol_utils.h/
# .cpp, added for handoff.md "Not yet done" #7) actually reaches the real
# JSON-RPC/LSP layer, not just the unit-test level (test_symbol_utils.cpp
# tests pickBestSymbol() directly; test_hover.cpp/test_definition.cpp don't
# cover the cross-file kind-preference case at all).
#
# Fixture: tests/integration/fixtures/kind_tiebreak/
#   .svlsp.f              — lists all three .sv files below, so opening any
#                           one of them (via upward ProjectRegistry discovery)
#                           compiles all three into the shared DB. Without
#                           this, only the opened file's own symbols would
#                           exist and the cross-file lookup below couldn't
#                           happen at all.
#   aaa_signal_holder.sv — declares "DisambigTarget" as a Signal. Its path
#                           sorts alphabetically FIRST, so this is the row
#                           findSymbolsByName's plain `ORDER BY path, line`
#                           would return without the kind-preference tiebreak.
#   zzz_class_decl.sv    — declares the real "DisambigTarget" Class. Its path
#                           sorts alphabetically LAST.
#   user_ref.sv           — references "DisambigTarget" without declaring it;
#                           neither candidate file, so hover/definition here
#                           exercise the tiebreak's cross-file fallback tier.
#
# Line-number reference (1-based -> LSP 0-based):
#   user_ref.sv:7        -> LSP line 6, char 2  — "  DisambigTarget obj;"
#   zzz_class_decl.sv:5  -> LSP line 4, char 6  — "class DisambigTarget;"
#   aaa_signal_holder.sv:7 -> LSP line 6, char 8 — "  logic DisambigTarget;"

USER_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/kind_tiebreak/user_ref.sv"
SIGNAL_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/kind_tiebreak/aaa_signal_holder.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "hover: cross-file reference picks the Class, not the alphabetically-earlier Signal" \
        "definition: cross-file reference resolves into zzz_class_decl.sv, not aaa_signal_holder.sv" \
        "definition: cross-file reference resolves to the Class's declaration line" \
        "hover: same-file reference still wins over the Class (tier 1 unaffected)"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "kind-preference tiebreak (textDocument/hover, textDocument/definition)"
# ---------------------------------------------------------------------------

# Hover on "DisambigTarget" in user_ref.sv (neither candidate file) must
# describe the Class, not the alphabetically-earlier Signal.
run_test "hover: cross-file reference picks the Class, not the alphabetically-earlier Signal" \
    "(condition-case err
       (let* ((buf     (svlsp-test/open-file \"${USER_FIXTURE}\"))
              (ok      (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result  (when ok
                         (with-current-buffer buf
                           (lsp-request \"textDocument/hover\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 6 :character 2))))))
              (content (when result
                         (let ((c (gethash \"contents\" result)))
                           (when (hash-table-p c) (gethash \"value\" c))))))
         (svlsp-test/close-file buf)
         (if (and ok content
                  (string-match-p \"Class\" content)
                  (not (string-match-p \"Signal\" content)))
             t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Go-to-definition from the same position must land in zzz_class_decl.sv,
# never aaa_signal_holder.sv.
run_test "definition: cross-file reference resolves into zzz_class_decl.sv, not aaa_signal_holder.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${USER_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 6 :character 2))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"kind_tiebreak/zzz_class_decl.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# The resolved location must be the Class's own declaration line (LSP
# line 4, 0-based -- "class DisambigTarget;" at 1-based line 5).
run_test "definition: cross-file reference resolves to the Class's declaration line" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${USER_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 6 :character 2))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 4)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Regression guard: same-file preference (tier 1) must still win when the
# request DOES originate in one of the two candidate files -- hovering on
# "DisambigTarget" inside aaa_signal_holder.sv itself must describe the
# Signal declared right there, not the Class in the other file.
run_test "hover: same-file reference still wins over the Class (tier 1 unaffected)" \
    "(condition-case err
       (let* ((buf     (svlsp-test/open-file \"${SIGNAL_FIXTURE}\"))
              (ok      (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result  (when ok
                         (with-current-buffer buf
                           (lsp-request \"textDocument/hover\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 6 :character 8))))))
              (content (when result
                         (let ((c (gethash \"contents\" result)))
                           (when (hash-table-p c) (gethash \"value\" c))))))
         (svlsp-test/close-file buf)
         (if (and ok content (string-match-p \"Signal\" content)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
