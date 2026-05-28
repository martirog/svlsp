// bind.sv — bind construct, used as a test fixture.
// Exercises: bind to module type, hierarchical signal access, checker injection.

module adder_checker #(parameter int WIDTH = 8) (
    input logic [WIDTH-1:0] a,
    input logic [WIDTH-1:0] b,
    input logic [WIDTH:0]   sum
);
    always_comb begin
        assert (sum == ({1'b0, a} + {1'b0, b}))
            else $error("adder_checker: %0d + %0d != %0d", a, b, sum);
    end
endmodule

module adder #(parameter int WIDTH = 8) (
    input  logic [WIDTH-1:0] a,
    input  logic [WIDTH-1:0] b,
    output logic [WIDTH:0]   sum
);
    assign sum = {1'b0, a} + {1'b0, b};
endmodule

module reg_checker #(parameter int WIDTH = 8) (
    input logic             clk,
    input logic             rst_n,
    input logic [WIDTH-1:0] d,
    input logic [WIDTH-1:0] q
);
    property p_reset;
        @(posedge clk) !rst_n |=> (q == '0);
    endproperty
    assert property (p_reset);
endmodule

module reg_ff #(parameter int WIDTH = 8) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic [WIDTH-1:0] d,
    output logic [WIDTH-1:0] q
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) q <= '0;
        else        q <= d;
    end
endmodule

// Inject checkers into every instance of the respective module types.
// Parameter overrides (#(...)) in bind instantiation are not supported by Sv.g4's
// LL(*) prediction; use default parameters in the checker modules instead.
bind adder  adder_checker u_ck (.a(a), .b(b), .sum(sum));;
bind reg_ff reg_checker   u_ck (.clk(clk), .rst_n(rst_n), .d(d), .q(q));;
