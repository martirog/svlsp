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
