// macros.sv — named constants and conditional structure, used as a test fixture.
// Exercises: parameter, localparam, generate if, string parameter comparison.
// Note: backtick preprocessor directives (`define/`ifdef/`include) are resolved by
// a separate preprocessor pass (see plan.md §4.2a) before the ANTLR4 parser runs.
// This fixture demonstrates post-preprocessing equivalents parseable by the grammar.

module macro_demo #(
    parameter  int    WIDTH     = 8,
    parameter  string BUILD_CFG = "SIMULATION"
) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic [WIDTH-1:0] d,
    output logic [WIDTH-1:0] q
);
    localparam int MAX_VAL = (1 << WIDTH) - 1;
    localparam int LAST    = WIDTH - 1;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) q <= '0;
        else        q <= d;
    end

    // generate if mirrors `ifdef at elaboration time
    generate
        if (BUILD_CFG == "SIMULATION") begin : sim_checks
            always @(posedge clk) begin
                assert (int'(q) <= MAX_VAL)
                    else $error("q=%0d exceeds MAX_VAL=%0d", q, MAX_VAL);
            end
        end else begin : no_sim_checks
        end
    endgenerate

endmodule
