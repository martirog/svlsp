// dpi.sv — DPI-C imports and exports, used as a test fixture.
// Exercises: import "DPI-C", export "DPI-C", pure/context qualifiers, chandle.

// Pure C function: no side effects, no SV context needed
import "DPI-C" pure function int unsigned sv_crc32(
    input logic [7:0] data [],
    input int unsigned len
);

// Context function: may call back into SV
import "DPI-C" context function int sv_model_step(
    input  real time_ns,
    input  int  stimulus,
    output int  response
);

// DPI task: can consume simulation time
import "DPI-C" task sv_wait_event(input int timeout_ns);

// Export an SV function so C code can call it
export "DPI-C" function sv_callback;

module dpi_demo #(parameter int WIDTH = 8) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic [WIDTH-1:0] d,
    output logic [WIDTH-1:0] q
);
    int unsigned crc;
    int          model_resp;
    logic [7:0]  buf_data [1];

    // Exported SV function callable from C
    function automatic void sv_callback(input int event_id);
        $display("sv_callback: event=%0d at t=%0t", event_id, $time);
    endfunction

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            q          <= '0;
            crc        <= 0;
            model_resp <= 0;
        end else begin
            q          <= d;
            buf_data[0] = d;
            crc        <= sv_crc32(buf_data, 1);
            // Sv.g4 uses `void(subroutine_call)` not `void'(...)` for void casts.
            void(sv_model_step($realtime, int'(d), model_resp));
        end
    end
endmodule
