// prototype_methods.sv — fixture for plan.md §6.16 function/task-prototype
// recording (pure virtual, extern, interface-class methods), test_33_prototype_methods.sh.
//
// Mirrors the real-world shape that motivated this fix: Policy is an
// interface class (always prototype-only, declared inside a package since
// that's the one reachable placement plan.md §6.16 wires up — see its
// grammar-fix section) with a pure-virtual describe(); PolicyImpl is a
// regular, top-level class extending an unresolved external base
// (uvm_object, never declared in this project — models UVM's own
// uvm_object the way the real bug report did) with its own pure-virtual
// get_policy(); all_policies is a queue of PolicyImpl, reusing §6.15's
// indexed dot-completion. Before this fix, neither method would ever show
// up anywhere (hover, completion, documentSymbol) — both are
// prototype-only, so the tree-walker never recorded them as symbols at
// all, and the file wouldn't even parse cleanly (interface_class_declaration
// was dead grammar).
//
// PolicyImpl is deliberately declared at *top level*, not inside a
// package, unlike Policy -- a separate, pre-existing dot-completion
// limitation (findSymbolsInScope requires an exact match against a
// class's fully-qualified scope, e.g. "policy_pkg::PolicyImpl", but
// userTypeName() deliberately records only the bare class name) means
// dot-completion can never resolve into a package-nested class's members
// today. That's a real, separate bug, out of scope for §6.16 -- see
// handoff.md. Keeping PolicyImpl top-level here avoids conflating the two.
//
// "// probe: ..." comment lines put the dot-completion trigger text at a
// precise position via search-forward on literal fixture text, not a
// hand-counted line/char position -- see test_32_live_edit_completion.sh's
// own header comment for why.

package policy_pkg;

  interface class Policy;
    pure virtual function string describe();
  endclass

endpackage

virtual class PolicyImpl extends uvm_object;
  pure virtual function PolicyImpl get_policy(uvm_object par);
endclass

module prototype_methods_top;
  PolicyImpl all_policies[$];

  // probe: all_policies[0].get

endmodule
