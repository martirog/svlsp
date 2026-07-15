// fp_defs_inc.sv — `included by fp_top.sv; defines a macro and a sub-module.
`define FP_BUS_WIDTH 16

module fp_sub_block (
    input  logic clk,
    output logic done
);
    assign done = clk;
endmodule
