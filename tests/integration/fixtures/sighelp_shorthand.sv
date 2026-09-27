// sighelp_shorthand.sv — fixture for test_12_signature_help.sh: comma-
// shorthand parameters (LRM 13.3). `shtf_b` inherits `input int` from
// `shtf_a`; the cursor is on the second argument of the call.
module shtf_top;
  function int shtf_add(input int shtf_a, shtf_b, output bit shtf_c);
    shtf_add = shtf_a + shtf_b;
  endfunction
  bit shtf_x;
  initial shtf_add(1, 2, shtf_x);
endmodule
