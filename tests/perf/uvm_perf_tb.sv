// uvm_perf_tb.sv -- fixture for the latency benchmarks (tests/perf/uvm.json,
// run by tools/lsp_perf.py and tests/perf/emacs_perf.sh).
//
// A small user testbench on top of the full UVM library DB: each request
// kind runs against ~18k library symbol rows. Requests find their position
// by text (see tests/perf/README.md), so lines can move freely. Completion
// probes sit under a never-defined `ifdef, as in uvm_corpus/uvm_tb.sv.

package perf_pkg;
  import uvm_pkg::*;

  class perf_env extends uvm_component;
    uvm_component perf_peer;
    int unsigned  perf_count;

    function new(string name, uvm_component parent);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      perf_count = perf_peer.get_num_children();
      `uvm_info("PERF", perf_peer.get_full_name(), UVM_LOW)
      void'(perf_peer.lookup("child"));
      perf_count++;
`ifdef SVLSP_TEST_PROBES
      probe-empty:
      probe-prefix: get_
      probe-dot: perf_peer.
      probe-chain: this.get_parent().
`endif
    endfunction

    function void report_phase(uvm_phase phase);
      set_report_verbosity_level(perf_count);
    endfunction
  endclass
endpackage

module perf_top;
  import uvm_pkg::*;
  perf_pkg::perf_env env;
`ifdef SVLSP_TEST_PROBES
  probe-lib: uvm_comp
  probe-outside: env.
`endif
endmodule
