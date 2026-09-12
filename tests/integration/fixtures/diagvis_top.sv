// diagvis_top.sv — clean primary file that `include`s a broken one.
// Used by test_37_diagnostics_visibility.sh (plan.md's "LSP
// diagnostics-visibility gap" fix): proves the server publishes
// diagnostics for diagvis_inc.sv too, not just this opened file.
`include "diagvis_inc.sv"

module diagvis_top_mod;
endmodule
