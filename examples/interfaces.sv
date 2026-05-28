// interfaces.sv — interface declaration with modport, used as a test fixture.
// Exercises: interface, modport, parameterized interface, modport port.

interface bus_if #(parameter int WIDTH = 8) (input logic clk);
    logic [WIDTH-1:0] data;
    logic             valid;
    logic             ready;

    modport master (output data, valid, input ready, input clk);
    modport slave  (input  data, valid, output ready, input clk);
endinterface

module producer #(parameter int WIDTH = 8) (
    bus_if.master bus,
    input  logic  clk,
    input  logic  rst_n
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            bus.data  <= '0;
            bus.valid <= 1'b0;
        end else if (bus.ready) begin
            bus.data  <= bus.data + 1;
            bus.valid <= 1'b1;
        end
    end
endmodule

module consumer #(parameter int WIDTH = 8) (
    bus_if.slave bus,
    input  logic  clk,
    input  logic  rst_n,
    output logic [WIDTH-1:0] captured
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            captured  <= '0;
            bus.ready <= 1'b1;
        end else if (bus.valid && bus.ready) begin
            captured  <= bus.data;
        end
    end
endmodule
