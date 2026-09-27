// package_scoped_dot_completion.sv — fixture for plan.md §6.17 dot-completion
// into a package-nested class's members, test_34_package_scoped_dot_completion.sh.
//
// Directly re-creates the real motivating shape: PolicyBase extends an
// unresolved external base (uvm_object, never declared in this project,
// modeling UVM's own uvm_object the way the real bug report did) and is
// declared *inside a package* (unlike every other completion fixture in
// this repo, which happens to declare its classes at top level -- see
// handoff.md's "Known gaps" entry for why that's exactly what let this bug
// go unnoticed for so long). Before this fix, dot-completion on a
// PolicyBase-typed variable only ever offered the synthetic
// randomize-family methods, never PolicyBase's own real, declared method.
//
// "probe: ..." lines (in a never-defined `ifdef) put the dot-completion trigger text at a
// precise position via search-forward on literal fixture text, not a
// hand-counted line/char position -- see test_32_live_edit_completion.sh's
// own header comment for why.

package policy_base_pkg;

  virtual class PolicyBase extends uvm_object;
    function PolicyBase get_policy(uvm_object par); endfunction
  endclass

endpackage

module package_scoped_dot_completion_top;
  import policy_base_pkg::*;

  PolicyBase policy;
`ifdef SVLSP_TEST_PROBES
     probe: policy.get
`endif
endmodule
