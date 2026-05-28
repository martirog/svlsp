// functions_tasks.sv — functions and tasks, used as a test fixture.
// Exercises: function, task, automatic, return, ref, void task.

module func_task_demo;
    function automatic logic [7:0] byte_reverse(input logic [7:0] val);
        logic [7:0] result;
        for (int i = 0; i < 8; i++)
            result[i] = val[7-i];
        return result;
    endfunction

    function automatic int unsigned clog2(input int unsigned val);
        int unsigned result;
        if (val <= 1) return 0;
        result = 0;
        val = val - 1;
        while (val > 0) begin
            val >>= 1;
            result++;
        end
        return result;
    endfunction

    task automatic drive_bus(
        ref   logic [7:0] bus,
        input logic [7:0] val,
        input logic       clk
    );
        @(posedge clk);
        bus = val;
    endtask

    task automatic wait_cycles(
        input int unsigned n,
        input logic        clk
    );
        repeat (n) @(posedge clk);
    endtask

    logic [7:0] data;
    logic       clk = 0;
    always #5 clk = ~clk;

    initial begin
        data = byte_reverse(8'hA5);
        $display("reversed 0xA5 = 0x%02h", data);
        $display("clog2(8) = %0d", clog2(8));
        drive_bus(data, 8'h42, clk);
        wait_cycles(3, clk);
        $finish;
    end
endmodule
