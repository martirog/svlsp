// ml_level2.sv — level 2 (middle): included from ml_top.sv, itself includes
// ml_level3.sv. ML_LEVEL3_FLAG is defined in ml_level3.sv and textually
// inserted just above this `ifdef by the nested include -- proves an `ifdef
// in the middle file correctly sees a macro defined by the deepest file.
`define ML_LEVEL2_SCALE 2
`include "ml_level3.sv"

`ifdef ML_LEVEL3_FLAG
module ml_level2_mod;
    logic [`ML_LEVEL2_SCALE-1:0] scale_sig;
endmodule
`endif

// Negative case: ML_NEVER_DEFINED is never defined anywhere in the include
// chain, so ml_should_not_exist must never appear in output, diagnostics, or
// any DB query. Without this, a test could pass even if `ifdef were
// accidentally short-circuited to "always true".
`ifdef ML_NEVER_DEFINED
module ml_should_not_exist;
endmodule
`endif
