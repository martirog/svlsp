// sighelp_macros.sv — fixture for test_12_signature_help.sh, plan.md §6.29
// part A: macro signature help. The cursor right after the second comma of
// the `SH29M_LOG( call is on its defaulted VERB parameter.
`include "sighelp_macros.svh"

module sh29m_top;
  initial begin
    `SH29M_LOG("sh29m", "hello", 2);
  end
endmodule
