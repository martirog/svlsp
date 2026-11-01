// tref_use.sv — fixture for test_52_type_references.sh (plan.md §6.21
// step 3): tref_item is imported, tref_missing is declared nowhere.

module tref_use;
  import tref_pkg::*;
  tref_item ok_item;
  tref_missing bad_item;
endmodule
