// generate.sv — generate blocks, used as a test fixture.
// Exercises: generate for, generate if, genvar, labeled generate blocks.

module carry_ripple #(
    parameter int WIDTH = 8
) (
    input  logic [WIDTH-1:0] a,
    input  logic [WIDTH-1:0] b,
    input  logic             cin,
    output logic [WIDTH-1:0] sum,
    output logic             cout
);
    logic [WIDTH:0] carry;
    assign carry[0] = cin;

    genvar i;
    generate
        for (i = 0; i < WIDTH; i++) begin : bit_gen
            assign sum[i]     = a[i] ^ b[i] ^ carry[i];
            assign carry[i+1] = (a[i] & b[i]) | (carry[i] & (a[i] ^ b[i]));
        end
    endgenerate

    assign cout = carry[WIDTH];
endmodule

module mux_tree #(
    parameter int N     = 4,
    parameter int WIDTH = 8
) (
    input  logic [N-1:0][WIDTH-1:0]  in,
    input  logic [$clog2(N)-1:0]     sel,
    output logic [WIDTH-1:0]         out
);
    generate
        if (N == 1) begin : passthrough
            assign out = in[0];
        end else begin : select
            assign out = in[sel];
        end
    endgenerate
endmodule

module parity_tree #(
    parameter int WIDTH = 8
) (
    input  logic [WIDTH-1:0] data,
    output logic             parity
);
    genvar j;
    logic [WIDTH-1:0] xor_chain;

    generate
        assign xor_chain[0] = data[0];
        for (j = 1; j < WIDTH; j++) begin : xor_gen
            assign xor_chain[j] = xor_chain[j-1] ^ data[j];
        end
    endgenerate

    assign parity = xor_chain[WIDTH-1];
endmodule
