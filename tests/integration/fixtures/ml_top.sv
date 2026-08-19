// ml_top.sv — level 1 (top, opened directly by the test): defines
// ML_TOP_WIDTH before including ml_level2.sv, which itself includes
// ml_level3.sv. ml_top_mod uses both ML_TOP_WIDTH (defined right here) and
// ML_LEVEL3_DEPTH (defined two include-levels down, in ml_level3.sv) --
// proves a macro survives back up to the top once the whole include chain
// unwinds.
`define ML_TOP_WIDTH 8
`include "ml_level2.sv"

module ml_top_mod;
    logic [`ML_TOP_WIDTH-1:0]    data;
    logic [`ML_LEVEL3_DEPTH-1:0] depth_sig;

    ml_level2_mod u_level2 ();
    ml_level3_mod u_level3 ();
endmodule
