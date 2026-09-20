// callargs_missing.sv — the call site omits `b`, which has no default.
// Used by test_39_missing_argument_diagnostic.sh (plan.md §6.23): confirms
// the missing-required-argument diagnostic reaches a real Emacs/lsp-mode
// client, not just SymbolDatabase directly.
module callargs_missing_mod;
  function int callargs_missing_func(int a, int b);
    callargs_missing_func = a + b;
  endfunction
  initial callargs_missing_func(1);
endmodule
