// top.sv — Phase 6.2 Stage 6 end-to-end fixture. leaf_mod is never declared
// or listed anywhere in this file; it must be resolved via the project's
// `-y libs` + `+libext+.sv` library search (see .svlsp.f) when top.sv is
// opened and its directory's .svlsp.f is discovered by ProjectRegistry.
module top;
    leaf_mod u_leaf ();
endmodule
