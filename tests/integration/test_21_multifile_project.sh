#!/usr/bin/env bash
# test_21_multifile_project.sh — Phase 6.2 end-to-end: a `.svlsp.f` filelist
# discovered by ProjectRegistry drives library resolution (`-y`/`+libext+`),
# and the resolved file's symbols are reachable via hover/definition with
# ZERO changes to HoverProvider/DefinitionProvider (they already do global,
# unscoped name lookup -- see handoff.md "Phase 6.2 Stage 1" for why).
#
# Fixture layout (tests/integration/fixtures/multifile_project/):
#   .svlsp.f        — "top.sv" + "-y libs" + "+libext+.sv"
#   top.sv          — module top; leaf_mod u_leaf (); endmodule
#                      (leaf_mod is NOT declared or listed anywhere in top.sv
#                      or .svlsp.f's explicit file list -- it can only be
#                      found by searching -y libs)
#   libs/leaf_mod.sv — module leaf_mod (); endmodule (library-resolved only)
#
# Deviation from the original plan, noted for whoever picks this up next:
# the plan additionally called for pointing the server at .svlsp.f via
# `initializationOptions.svlsp.projectConfig` (set through a dynamic
# `svlsp-test/initialization-options' variable, reset to nil afterward) to
# exercise ProjectRegistry's explicit-path override end-to-end. That
# variable IS wired up in tools/emacs-test-init.el for future use, but this
# test does not use it: lsp-mode reuses one workspace/server process per
# detected project root, every fixture in this repo resolves to the same
# git-root workspace, and that workspace's `initialize` handshake (and thus
# its ServerState::explicitProjectConfigPath) already happened earlier in
# the suite (as early as test_02) -- mutating the variable now would have
# no effect without a `lsp-workspace-restart', which risks destabilizing
# every test that runs after this one. The explicit-path-override behavior
# itself is already fully covered at the unit level (test_project_registry.
# cpp, "an explicit config path overrides upward search for every file").
# This test instead proves the upward-search discovery path (Stage 5's
# actual runtime behavior for every real editor session, since nobody hand-
# configures initializationOptions in practice): opening top.sv triggers
# ProjectRegistry::configFor to discover .svlsp.f sitting right there in
# top.sv's own directory (first candidate checked, no need to search
# further up), which is just as strong a proof of the end-to-end pipeline.
#
# Line-number reference (1-based -> LSP 0-based):
#   top.sv:6            -> LSP line 5, char 4  — "    leaf_mod u_leaf ();"  word = "leaf_mod"
#   libs/leaf_mod.sv:3  -> LSP line 2, char 7  — "module leaf_mod ();"      word = "leaf_mod"

TOP_FIXTURE="${SVLSP_ROOT}/tests/integration/fixtures/multifile_project/top.sv"

# ---------------------------------------------------------------------------
# Guard
# ---------------------------------------------------------------------------
if [ ! -x "${SVLSP_BIN}" ]; then
    for name in \
        "hover: leaf_mod instantiation site returns non-null" \
        "hover: content mentions leaf_mod" \
        "definition: leaf_mod resolves into libs/leaf_mod.sv" \
        "definition: leaf_mod resolves to its declaration line"
    do
        skip_test "$name" "svlsp binary not found at ${SVLSP_BIN}"
    done
    return
fi

# ---------------------------------------------------------------------------
section "multi-file project (.svlsp.f + -y library resolution) — hover"
# ---------------------------------------------------------------------------

# Hover on "leaf_mod" at its instantiation site in top.sv. leaf_mod is only
# declared in libs/leaf_mod.sv, reachable solely via -y library resolution
# triggered by ProjectRegistry discovering .svlsp.f in top.sv's directory.
run_test "hover: leaf_mod instantiation site returns non-null" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/hover\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 5 :character 4)))))))
         (svlsp-test/close-file buf)
         (if (and ok (not (null result))) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

run_test "hover: content mentions leaf_mod" \
    "(condition-case err
       (let* ((buf     (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok      (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result  (when ok
                         (with-current-buffer buf
                           (lsp-request \"textDocument/hover\"
                                        (list :textDocument (list :uri (lsp--buffer-uri))
                                              :position     (list :line 5 :character 4))))))
              (content (when result
                         (let ((c (gethash \"contents\" result)))
                           (when (hash-table-p c) (gethash \"value\" c))))))
         (svlsp-test/close-file buf)
         (if (and ok content (string-match-p \"leaf_mod\" content)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# ---------------------------------------------------------------------------
section "multi-file project (.svlsp.f + -y library resolution) — go-to-definition"
# ---------------------------------------------------------------------------

# Definition of "leaf_mod" from top.sv must resolve into libs/leaf_mod.sv --
# a file that was never opened by the editor and never listed as an
# explicit project file, only discovered via -y library resolution.
run_test "definition: leaf_mod resolves into libs/leaf_mod.sv" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 5 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (uri    (when loc (gethash \"uri\" loc))))
         (svlsp-test/close-file buf)
         (if (and ok uri (string-suffix-p \"libs/leaf_mod.sv\" uri)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"

# Definition must report line 2 (0-based) -- leaf_mod's declaration line in
# libs/leaf_mod.sv.
run_test "definition: leaf_mod resolves to its declaration line" \
    "(condition-case err
       (let* ((buf    (svlsp-test/open-file \"${TOP_FIXTURE}\"))
              (ok     (with-current-buffer buf (svlsp-test/wait-for-lsp 15)))
              (result (when ok
                        (with-current-buffer buf
                          (lsp-request \"textDocument/definition\"
                                       (list :textDocument (list :uri (lsp--buffer-uri))
                                             :position     (list :line 5 :character 4))))))
              (loc    (when result (if (vectorp result) (aref result 0) result)))
              (line   (when loc
                        (gethash \"line\"
                                 (gethash \"start\" (gethash \"range\" loc))))))
         (svlsp-test/close-file buf)
         (if (and ok (equal line 2)) t nil))
     (error (format \"elisp-error: %s\" (error-message-string err))))" \
    "t"
