// timescale.sv — time units, precision, and time literals, used as a test fixture.
// Exercises: timeunit, timeprecision, time literals (ns/ps), $time, $realtime, $timeformat.
// Note: the `timescale compiler directive is resolved by the preprocessor (plan.md §4.2a).
// The grammar equivalent is the timeunit/timeprecision declaration inside the module.

module timescale_demo (
    output logic clk,
    output logic rst_n
);
    timeunit      1ns;
    timeprecision 1ps;

    // 100 MHz clock: 10 ns period, 5 ns half-period
    initial clk = 1'b0;
    always  #5ns clk = ~clk;

    // Reset sequence
    initial begin
        $timeformat(-9, 3, " ns", 12);
        rst_n = 1'b0;
        #20ns;
        rst_n = 1'b1;
        $display("Reset released at %t (realtime %0.3f ns)", $time, $realtime);
        #100ns;
        $finish;
    end
endmodule

// Second module: timeunit/timeprecision as a package-style declaration and realtime param
module time_annot_demo #(
    parameter realtime CLK_PERIOD = 10ns
) (
    output logic clk
);
    timeunit      1ns;
    timeprecision 1ps;

    initial clk = 1'b0;
    always  #(CLK_PERIOD / 2) clk = ~clk;
endmodule
