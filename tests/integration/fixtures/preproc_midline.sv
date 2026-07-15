// preproc_midline.sv — macro invocation in the middle of a line (not at the
// start), used to verify that symbol *columns* after a mid-line macro
// expansion are still reported relative to the original (unexpanded) source
// that the editor buffer actually contains, not the internal expanded text.
`define WIDTH 8

module midline_mod;
    wire [`WIDTH-1:0] data_bus;
    wire              valid_out;

    assign valid_out = |data_bus;
endmodule
