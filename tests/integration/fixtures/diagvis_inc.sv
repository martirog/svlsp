// diagvis_inc.sv — intentionally broken, `include`d only by diagvis_top.sv.
// Never opened directly by test_37_diagnostics_visibility.sh -- its own
// diagnostics must reach the client purely via the server's push, keyed by
// this file's own URI, not diagvis_top.sv's.
module diagvis_broken_mod {
  logic x;
endmodule
