// rcvr_top.sv — fixture for test_47_receivers.sh (plan.md §6.30,
// "Follow-up: struct, interface and hierarchical receivers"): struct,
// interface (instance, virtual interface, interface port) and hierarchical
// (instance, named block) receivers resolve to their own members. Every
// member name also exists in rcvr_decoy, declared first.

module rcvr_decoy;
  logic rcvr_hi, rcvr_valid, rcvr_sig;
endmodule

package rcvr_p;
  typedef struct { int rcvr_hi; int rcvr_lo; } rcvr_pair_t;
endpackage

interface rcvr_if(input logic clk);
  logic rcvr_valid;
  task automatic rcvr_drive(); endtask
endinterface

module rcvr_sub(input logic a);
  logic rcvr_sig;
endmodule

module rcvr_top(rcvr_if rcvr_port);
  import rcvr_p::*;
  logic clk;
  rcvr_pair_t s;
  rcvr_if rcvr_u_if(clk);
  virtual rcvr_if vif;
  rcvr_sub rcvr_u(.a(clk));
  initial begin : rcvr_blk
    int rcvr_bx;
    s.rcvr_hi = 1;
    vif.rcvr_valid = 1;
    rcvr_port.rcvr_valid = 0;
    rcvr_u.rcvr_sig = 0;
    rcvr_blk.rcvr_bx = 2;
  end
`ifdef SVLSP_TEST_PROBES
     probe: vif.
     probe: s.
     probe: rcvr_u.
`endif
endmodule

module rcvr_nb_sub;
  if (1) begin : rcvr_gen
    logic rcvr_w;
  end
  initial begin
    int rcvr_anon;
  end
endmodule

module rcvr_nb_top;
  rcvr_nb_sub rcvr_nb_u();
`ifdef SVLSP_TEST_PROBES
     probe: rcvr_nb_u.
     probe: rcvr_nb_u.rcvr_gen.
`endif
endmodule
