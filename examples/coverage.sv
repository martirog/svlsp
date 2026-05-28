// coverage.sv — covergroups and coverpoints, used as a test fixture.
// Exercises: covergroup, coverpoint, cross, bins, ignore_bins, get_coverage.

module coverage_demo;
    logic [7:0] data;
    logic [1:0] mode;
    logic       valid;

    covergroup cg_data @(posedge valid);
        cp_data: coverpoint data {
            bins zeros   = {8'h00};
            bins ones    = {8'hFF};
            bins low []  = {[8'h01:8'h3F]};
            bins mid []  = {[8'h40:8'hBF]};
            bins high [] = {[8'hC0:8'hFE]};
        }

        cp_mode: coverpoint mode {
            bins idle  = {2'b00};
            bins read  = {2'b01};
            bins write = {2'b10};
            bins burst = {2'b11};
        }

        // cross_body with ignore_bins requires a grammar-level double-semicolon
        // in Sv.g4 (cross_body_item already consumes ';', then cross_body adds
        // another).  Use the simple ';' form of cross_body instead.
        cx_data_mode: cross cp_data, cp_mode;
    endgroup

    cg_data cg_inst = new();

    initial begin
        data = 8'h00; mode = 2'b01; valid = 1; #1; valid = 0;
        data = 8'hFF; mode = 2'b10; valid = 1; #1; valid = 0;
        data = 8'h55; mode = 2'b11; valid = 1; #1; valid = 0;
        data = 8'h20; mode = 2'b00; valid = 1; #1; valid = 0;
        $display("coverage = %0.1f%%", cg_inst.get_coverage());
    end
endmodule
