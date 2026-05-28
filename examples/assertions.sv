// assertions.sv — SystemVerilog Assertions, used as a test fixture.
// Exercises: assert, assume, cover, sequence, property, concurrent assertions.

module sva_demo (
    input logic clk,
    input logic rst_n,
    input logic req,
    input logic ack,
    input logic [7:0] data
);
    // Immediate assertion
    always_comb begin
        assert (data != 8'hFF) else $warning("data must not be 0xFF");
    end

    // Sequence: req followed by ack within 1–4 cycles
    sequence seq_req_ack;
        req ##[1:4] ack;
    endsequence

    // Property: every req is eventually acknowledged
    property p_req_ack;
        @(posedge clk) disable iff (!rst_n)
        req |-> ##[1:4] ack;
    endproperty

    // Property: ack must not appear without a prior req
    property p_no_spurious_ack;
        @(posedge clk) disable iff (!rst_n)
        ack |-> $past(req, 1);
    endproperty

    // Property: req does not fire two consecutive cycles while ack is low
    property p_no_double_req;
        @(posedge clk) disable iff (!rst_n)
        (req && !ack) |=> !req;
    endproperty

    a_req_ack:       assert property (p_req_ack)
                         else $error("req not acknowledged in time");
    a_no_spurious:   assert property (p_no_spurious_ack);
    a_no_double_req: assert property (p_no_double_req);

    // Assume (used in formal verification)
    assume property (@(posedge clk) disable iff (!rst_n) !($isunknown(req)));

    // Cover
    c_req_ack: cover property (seq_req_ack);
endmodule
