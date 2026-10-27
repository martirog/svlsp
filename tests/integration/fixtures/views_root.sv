// views_root.sv — fixture for test_49_views_root.sh (plan.md §6.30
// leftovers): modports and clocking blocks as views of their interface,
// and $root paths. Every member name also exists in vwr_decoy, declared
// first.
module vwr_decoy;
  logic vwr_data, vwr_valid, vwr_sig;
endmodule
interface vwr_if(input logic clk);
  logic [7:0] vwr_data;
  logic vwr_valid, vwr_only;
  task automatic vwr_drive(); endtask
  clocking vwr_cb @(posedge clk);
    input vwr_data;
  endclocking
  modport vwr_drv(input vwr_data, output vwr_valid, import vwr_drive, clocking vwr_cb);
endinterface

module vwr_sub;
  logic vwr_sig;
endmodule

module vwr_top;
  logic clk, x;
  vwr_sub vwr_u();
  virtual vwr_if vif;
  virtual vwr_if.vwr_drv vdrv;
  initial begin
    x = vif.vwr_cb.vwr_data;
    vdrv.vwr_valid = 1;
    x = $root.vwr_top.vwr_u.vwr_sig;
  end
`ifdef SVLSP_TEST_PROBES
     probe: vdrv.
     probe: $root.
`endif
endmodule
