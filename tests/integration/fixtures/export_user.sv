// export_user.sv — imports middle_pkg only; base_pkg symbols must be visible via export.
import middle_pkg::*;

module export_user (
    input  logic clk,
    output logic done
);
    Beta  b;
    Alpha a;
    // LSP line 9 (0-based), char 0 — completion trigger, inside export_user body.
endmodule
