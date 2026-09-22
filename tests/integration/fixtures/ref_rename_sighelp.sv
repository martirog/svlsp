// ref_rename_sighelp.sv — fixture for test_07_references.sh/
// test_11_rename.sh/test_12_signature_help.sh (plan.md item 3). Every
// declared name here is prefixed `rrsh_` -- this repo's Emacs test daemon
// shares one svlsp server process and one growing in-memory DB across every
// test_*.sh file (never purged on didClose), so an unprefixed, common name
// like "adder"/"top"/"data" risks colliding with an unrelated fixture or
// example elsewhere in the tree (module_basic.sv already declares its own
// "adder", which a first draft of this fixture collided with directly).
module rrsh_adder (
  input  logic rrsh_clk,
  input  logic rrsh_rst_n,
  output logic rrsh_done
);
endmodule

module rrsh_top;
  logic rrsh_data;
  logic rrsh_result;

  assign rrsh_result = rrsh_data;
  assign rrsh_data = 1;

  rrsh_adder rrsh_u_inst (
    .rrsh_clk(rrsh_clk),
    .rrsh_rst_n(rrsh_rst_n),
    .rrsh_done(
  );
endmodule

// rrsh_compute -- fixture for the plan.md §6.22 follow-up: signature help
// on a bare (undotted) function call. Two parameters, the second with a
// default value, so the same fixture also covers default-value rendering.
function automatic int rrsh_compute(input int rrsh_a, input int rrsh_b = 4);
  return rrsh_a + rrsh_b;
endfunction

module rrsh_caller;
  logic [31:0] rrsh_out;
  initial begin
    rrsh_out = rrsh_compute(rrsh_out,
  end
endmodule

// rrsh_sh_* -- fixture for plan.md §6.27: dotted-call signature help,
// covering different scoping rules -- own-class method, inherited method
// (via extends), a two-segment field chain, a class resolved through a
// specific import, and a class referenced by its full package-qualified
// name without ever being imported at all.
package rrsh_sh_pkg;
  class rrsh_sh_base;
    function int rrsh_sh_base_get(int rrsh_bx, int rrsh_by = 5);
      return rrsh_bx + rrsh_by;
    endfunction
  endclass

  class rrsh_sh_child extends rrsh_sh_base;
    function int rrsh_sh_child_get(int rrsh_cx);
      return rrsh_cx;
    endfunction
  endclass
endpackage

import rrsh_sh_pkg::rrsh_sh_child;

class rrsh_sh_holder;
  rrsh_sh_pkg::rrsh_sh_child rrsh_sh_field;
endclass

module rrsh_sh_caller;
  rrsh_sh_child  rrsh_sh_obj;
  rrsh_sh_holder rrsh_sh_holder_obj;
  initial begin
    rrsh_sh_obj.rrsh_sh_child_get(
    rrsh_sh_obj.rrsh_sh_base_get(
    rrsh_sh_holder_obj.rrsh_sh_field.rrsh_sh_child_get(
  end
endmodule

// rrsh_p28_* -- fixture for plan.md §6.28: module port type information in
// signature help. rrsh_p28_wide has a typed `int` port and a defaulted
// `bit` port, previously rendered with direction only ("input"/"output"),
// now with each port's own declared type too.
module rrsh_p28_wide (
  input  int rrsh_p28_width,
  output bit rrsh_p28_valid = 1
);
endmodule

module rrsh_p28_caller;
  rrsh_p28_wide rrsh_p28_u1 (
  );
endmodule
