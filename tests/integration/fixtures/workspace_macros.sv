// workspace_macros.sv — fixture for test_10_workspace_symbols.sh: a
// `define is listed by workspace/symbol (macros live in their own table).
// `wsmac_LOG is on line 4 (LSP line 3), its name at char 8.
`define wsmac_LOG(MSG) $display(MSG)
module wsmac_top;
  initial `wsmac_LOG("hi")
endmodule
