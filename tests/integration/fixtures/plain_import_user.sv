// plain_import_user.sv — imports plain_middle_pkg only; base_pkg symbols must NOT leak through.
import plain_middle_pkg::*;

module plain_import_user (
    input  logic clk,
    output logic done
);
    Gamma g;
    // LSP line 8 (0-based), char 0 — completion trigger; Alpha must be absent, Gamma present.
endmodule
