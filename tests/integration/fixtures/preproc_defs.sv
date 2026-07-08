// preproc_defs.sv — included file for preprocessor source-map tests.
`define BUS_W 8

module sub_block (
    input  logic         clk,
    input  logic [7:0]   din,
    output logic [7:0]   dout
);
    assign dout = din;
endmodule
