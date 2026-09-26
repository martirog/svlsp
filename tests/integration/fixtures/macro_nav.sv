// macro_nav.sv — fixture for test_41_macro_navigation.sh: hover,
// definition and references on a macro from an included header, next to a
// same-named parameter that must stay separate.
`include "macro_nav.svh"

module mnav_top;
  parameter int MNAV_LOG = 1;
  initial begin
    `MNAV_LOG("a", "b");
    `MNAV_LOG("c");
  end
endmodule
