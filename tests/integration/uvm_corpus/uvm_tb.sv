// uvm_tb.sv -- fixture for tests/integration/uvm_corpus/uvm_completion.sh
// (opt-in: needs a UVM library DB built with `svlsp --build-db`).
//
// A small user testbench on top of the full UVM library DB: every
// completion kind runs against ~15k library symbol rows, so the same
// requests double as a profiling harness for library-scale completion.
// Probes sit under a never-defined `ifdef (see dot_completion.sv's header
// for why); the test finds each by its text, so lines can move freely.

package tbc_pkg;
  import uvm_pkg::*;

  class tbc_env extends uvm_component;
    uvm_component tbc_peer;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
`ifdef SVLSP_TEST_PROBES
      probe-arg: phase.
      probe-prop: tbc_peer.
      probe-super: super.
      probe-chain: this.get_parent().
      probe-inherited: get_
      probe-access: m_inst
      probe-empty: 
`endif
    endfunction

    function void collect(ref uvm_component kids[$]);
`ifdef SVLSP_TEST_PROBES
      probe-queue: kids.
      probe-elem: kids[0].
`endif
    endfunction
  endclass
endpackage

module tbc_top;
  import uvm_pkg::*;
  uvm_component tbc_comp;
`ifdef SVLSP_TEST_PROBES
  probe-lib: uvm_comp
  probe-fuzzy: uvmcmpnt
  probe-outside: tbc_comp.
`endif
endmodule
