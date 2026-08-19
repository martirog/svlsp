// ml_level3.sv — level 3 (deepest): included from ml_level2.sv.
// Defines ML_LEVEL3_FLAG (consumed by an `ifdef in ml_level2.sv, the middle
// file, proving upward `ifdef visibility from the deepest include) and
// ML_LEVEL3_DEPTH (consumed by ml_top.sv, two include-levels up).
// ml_level3_mod itself uses ML_TOP_WIDTH, defined in ml_top.sv two
// include-levels ABOVE this file -- proves downward macro visibility
// through more than one nested include.
`define ML_LEVEL3_FLAG
`define ML_LEVEL3_DEPTH 4

module ml_level3_mod;
    logic [`ML_TOP_WIDTH-1:0] top_width_sig;
endmodule
