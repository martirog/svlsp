// packages.sv — package declaration and import, used as a test fixture.
// Exercises: package, import, typedef, parameter, function inside package.

package math_pkg;
    parameter int unsigned PI_FIXED = 32'h6487_ED51;

    typedef logic [15:0] fixed16_t;

    function automatic fixed16_t saturate(input int val);
        if (val > 16'h7FFF) return 16'h7FFF;
        if (val < -16'h8000) return 16'h8000;
        return fixed16_t'(val);
    endfunction
endpackage

package bus_pkg;
    typedef enum logic [1:0] {
        CMD_NOP   = 2'b00,
        CMD_READ  = 2'b01,
        CMD_WRITE = 2'b10,
        CMD_BURST = 2'b11
    } cmd_t;

    typedef struct packed {
        cmd_t        cmd;
        logic [13:0] addr;
        logic [15:0] data;
    } txn_t;
endpackage

module bus_master
    import bus_pkg::*;
    import math_pkg::saturate;
#(
    parameter int ADDR_W = 14
) (
    input  logic  clk,
    input  logic  rst_n,
    output txn_t  txn,
    output logic  txn_valid
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            txn       <= '0;
            txn_valid <= 1'b0;
        end else begin
            txn.cmd   <= CMD_READ;
            txn.addr  <= txn_t'(saturate(int'(txn.addr) + 4)).addr;
            txn_valid <= 1'b1;
        end
    end
endmodule
