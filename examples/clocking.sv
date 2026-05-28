// clocking.sv — clocking blocks, used as a test fixture.
// Exercises: clocking, input/output skew, clocking block in modport.

interface mem_if (input logic clk);
    logic [15:0] addr;
    logic [31:0] data;
    logic        wr_en;
    logic        rd_en;
    logic        ack;

    clocking master_cb @(posedge clk);
        default input #1step output #2;
        output addr, data, wr_en, rd_en;
        input  ack;
    endclocking

    clocking monitor_cb @(posedge clk);
        default input #1step;
        input addr, data, wr_en, rd_en, ack;
    endclocking

    modport master  (clocking master_cb,  input clk);
    modport monitor (clocking monitor_cb, input clk);
endinterface

module mem_ctrl (mem_if.master mif);
    always @(mif.master_cb) begin
        if (!mif.master_cb.ack) begin
            mif.master_cb.wr_en <= 1'b0;
            mif.master_cb.rd_en <= 1'b0;
        end
    end
endmodule

module mem_monitor (mem_if.monitor mif);
    always @(mif.monitor_cb) begin
        if (mif.monitor_cb.wr_en)
            $display("t=%0t WRITE addr=%04h data=%08h",
                     $time, mif.monitor_cb.addr, mif.monitor_cb.data);
        if (mif.monitor_cb.rd_en)
            $display("t=%0t READ  addr=%04h",
                     $time, mif.monitor_cb.addr);
    end
endmodule
