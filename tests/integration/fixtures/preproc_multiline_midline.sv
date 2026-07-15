// preproc_multiline_midline.sv — a macro DEFINED across multiple physical
// lines (backslash-continuation) and then INVOKED in the middle of a line
// (not at line start). Checks that continuation lines are merged into a
// single macro body before expansion, and that mid-line invocation of such
// a macro still resolves correct symbol positions afterward.
`define WIDE_WIDTH \
    16

module multiline_midline_mod;
    wire [`WIDE_WIDTH-1:0] wide_bus;
    wire                   ready_out;

    assign ready_out = |wide_bus;
endmodule
