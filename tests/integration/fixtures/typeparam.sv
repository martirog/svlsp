// typeparam.sv — fixture for test_51_type_parameters.sh (plan.md §6.21
// step 1): a type parameter is a declaration, and a value of its type
// resolves through its default. tprm_decoy declares tprm_get first.

class tprm_decoy; function int tprm_get(); return 1; endfunction endclass
package tprm_p;
  class tprm_item;
    int tprm_field;
    function int tprm_get(); return 0; endfunction
  endclass
  class tprm_box #(type T = tprm_item, type V = int);
    T tprm_t;
    V tprm_v;
    function void tprm_f();
      void'(tprm_t.tprm_get());
`ifdef SVLSP_TEST_PROBES
      tprm_t.
`endif
    endfunction
  endclass
endpackage
module tprm_top;
  import tprm_p::*;
  tprm_box #(.T(tprm_item)) tprm_b;
endmodule
