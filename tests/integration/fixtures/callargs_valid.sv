// callargs_valid.sv — every declared parameter is supplied at the call site.
// Used by test_39_missing_argument_diagnostic.sh (plan.md §6.23) as the
// "stays clean" counterpart to callargs_missing.sv.
module callargs_valid_mod;
  function int callargs_valid_func(int a, int b);
    callargs_valid_func = a + b;
  endfunction
  initial callargs_valid_func(1, 2);
endmodule
