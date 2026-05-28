// always_blocks.sv — procedural blocks, used as a test fixture.
// Exercises: always_ff, always_comb, always_latch, sensitivity lists.

module proc_blocks #(
    parameter int WIDTH = 8
) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic             en,
    input  logic [WIDTH-1:0] d,
    output logic [WIDTH-1:0] q_ff,
    output logic [WIDTH-1:0] q_comb,
    output logic [WIDTH-1:0] q_latch
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            q_ff <= '0;
        else if (en)
            q_ff <= d;
    end

    always_comb begin
        if (en)
            q_comb = d;
        else
            q_comb = '0;
    end

    always_latch begin
        if (en)
            q_latch = d;
    end
endmodule
