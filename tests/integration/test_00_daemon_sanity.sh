#!/usr/bin/env bash
# test_00_daemon_sanity.sh — verify the Emacs daemon and init file are working
# correctly before any LSP server is involved.

section "Daemon sanity"

run_test "emacsclient round-trip" \
    "(+ 1 1)" \
    "2"

run_test "Emacs version is 25 or newer" \
    "(>= emacs-major-version 25)" \
    "t"

run_test "verilog-mode is available" \
    "(progn (require 'verilog-mode) t)" \
    "t"

section "lsp-mode"

run_test "lsp-mode is loaded" \
    "(featurep 'lsp-mode)" \
    "t"

run_test "svlsp client is registered" \
    "(if (gethash 'svlsp lsp-clients) t nil)" \
    "t"

run_test "clangd executable is set to an existing file" \
    "(and (stringp lsp-clients-clangd-executable) (file-executable-p lsp-clients-clangd-executable) t)" \
    "t"
