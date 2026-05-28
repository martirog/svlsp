// program.sv — program blocks, used as a test fixture.
// Exercises: program, initial, $exit, clocking block usage from a program.

interface simple_if (input logic clk);
    logic [7:0] data;
    logic       valid;

    clocking cb @(posedge clk);
        default input #1step output #1;
        input  data, valid;
        output data, valid;
    endclocking

    modport driver  (clocking cb, input clk);
    modport monitor (clocking cb, input clk);
endinterface

program driver_prog (simple_if.driver sif);
    initial begin
        sif.cb.valid <= 1'b0;
        sif.cb.data  <= 8'h00;
        @(sif.cb);

        for (int i = 0; i < 8; i++) begin
            sif.cb.data  <= 8'(i * 16);
            sif.cb.valid <= 1'b1;
            @(sif.cb);
        end

        sif.cb.valid <= 1'b0;
        repeat (2) @(sif.cb);
        $exit;
    end
endprogram

program monitor_prog (simple_if.monitor sif);
    initial begin
        $display("Monitor started at t=%0t", $time);
        forever begin
            @(sif.cb);
            if (sif.cb.valid)
                $display("t=%0t data=%02h", $time, sif.cb.data);
        end
    end
endprogram
