// preproc_main.sv — top file using `include for preprocessor source-map tests.
`include "preproc_defs.sv"

module top_wrapper (
    input  logic         clk,
    input  logic [7:0]   din,
    output logic [7:0]   dout
);
    sub_block u_sub (
        .clk(clk),
        .din(din),
        .dout(dout)
    );
endmodule
