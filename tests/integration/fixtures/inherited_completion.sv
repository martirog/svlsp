// inherited_completion.sv — fixture for test_16_class_scope_completion.sh
// (§6.30 follow-up): bare-name completion inside a derived class's method
// body offers the members it inherits. The cursor is at char 0 of the
// `icmp_own = 1;` line (LSP line 12), so the word prefix is empty.
class icmp_Base;
  int icmp_base_fld;
  function void icmp_base_m(); endfunction
endclass
class icmp_C extends icmp_Base;
  int icmp_own;
  function void run();
    int icmp_local;
    icmp_own = 1;
  endfunction
endclass
class icmp_Unrelated;
  int icmp_unrelated_fld;
endclass
