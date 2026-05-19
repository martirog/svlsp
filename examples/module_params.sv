// module_params.sv — module with multiple parameters, used as a test fixture.
// Exercises: parameterized types, localparam, generate.

module shift_reg #(
    parameter int WIDTH = 8,
    parameter int DEPTH = 4
) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic [WIDTH-1:0] d,
    output logic [WIDTH-1:0] q
);
    localparam int LAST = DEPTH - 1;

    logic [WIDTH-1:0] pipe [0:LAST];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            foreach (pipe[i]) pipe[i] <= '0;
        end else begin
            pipe[0] <= d;
            foreach (pipe[i]) if (i > 0) pipe[i] <= pipe[i-1];
        end
    end

    assign q = pipe[LAST];
endmodule
