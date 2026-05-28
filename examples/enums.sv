// enums.sv — enum types, used as a test fixture.
// Exercises: enum, typedef, unique case, enum methods (first, last, next, name).

module enum_demo (
    input  logic       clk,
    input  logic       rst_n,
    output logic [2:0] state_out
);
    typedef enum logic [2:0] {
        IDLE      = 3'b000,
        FETCH     = 3'b001,
        DECODE    = 3'b010,
        EXECUTE   = 3'b011,
        WRITEBACK = 3'b100,
        STALL     = 3'b101
    } state_t;

    state_t state, next_state;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) state <= IDLE;
        else        state <= next_state;
    end

    always_comb begin
        unique case (state)
            IDLE:      next_state = FETCH;
            FETCH:     next_state = DECODE;
            DECODE:    next_state = EXECUTE;
            EXECUTE:   next_state = WRITEBACK;
            WRITEBACK: next_state = IDLE;
            STALL:     next_state = FETCH;
            default:   next_state = IDLE;
        endcase
    end

    assign state_out = 3'(state);

    initial begin
        $display("first=%s last=%s", state_t.first.name(), state_t.last.name());
    end
endmodule
