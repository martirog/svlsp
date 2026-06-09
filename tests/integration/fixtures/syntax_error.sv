// syntax_error.sv — intentional parse error used by integration tests.
// The '{' in place of ';' after the module name is invalid SystemVerilog.
module syntax_error_module {
  logic x;
endmodule
