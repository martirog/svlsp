// fp_top.sv — Phase 6.2 full-project integration fixture: combines
// `include, wildcard + specific imports, transitive export resolution,
// macro expansion, an explicit project file, and -y library resolution in
// one project, checked against hover/definition/completion/documentSymbol/
// workspaceSymbol/diagnostics.
`include "fp_defs_inc.sv"

import fp_reexport_pkg::*;
import fp_util_pkg::fp_compute;

module fp_top;
    wire [`FP_BUS_WIDTH-1:0] fp_top_bus;

    FpExtra  fp_e;
    FpWidget fp_w;

    fp_sub_block u_sub (
        .clk(1'b0),
        .done(fp_top_bus[0])
    );

    fp_extra_mod u_extra ();

    fp_leaf_mod u_leaf ();
endmodule
