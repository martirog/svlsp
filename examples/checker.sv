// checker.sv — checker blocks, used as a test fixture.
// Exercises: checker, assert property, assume property, cover property inside checker.

checker handshake_check (
    input logic clk,
    input logic rst_n,
    input logic req,
    input logic ack
);
    // req must be acknowledged within 1–4 cycles
    property p_req_ack;
        @(posedge clk) disable iff (!rst_n)
        req |-> ##[1:4] ack;
    endproperty

    // ack must not appear without a preceding req
    property p_no_spurious_ack;
        @(posedge clk) disable iff (!rst_n)
        ack |-> $past(req, 1);
    endproperty

    // req must not stay high two cycles in a row while waiting
    property p_no_double_req;
        @(posedge clk) disable iff (!rst_n)
        (req && !ack) |=> !req;
    endproperty

    a_req_ack:        assert property (p_req_ack);
    a_no_spurious:    assert property (p_no_spurious_ack);
    a_no_double_req:  assert property (p_no_double_req);

    assume property (@(posedge clk) disable iff (!rst_n) !($isunknown(req)));

    c_handshake: cover property (@(posedge clk) req ##[1:4] ack);
endchecker

module dut (
    input logic clk,
    input logic rst_n,
    input logic req,
    input logic ack
);
    handshake_check hs_chk (
        .clk   (clk),
        .rst_n (rst_n),
        .req   (req),
        .ack   (ack)
    );
endmodule
