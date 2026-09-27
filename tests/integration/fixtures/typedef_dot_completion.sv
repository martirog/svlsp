// typedef_dot_completion.sv — fixture for test_27_dot_completion.sh (§6.30
// follow-up): `h`'s type is a typedef of a typedef of TddC, so `h.` offers
// TddC's members. The probe sits in a never-defined `ifdef (see
// dot_completion.sv's header for why).
package tdd_p;
  class TddC;
    int tdd_fld;
    function void tdd_run(); endfunction
  endclass
  typedef TddC tdd_alias_t;
  typedef tdd_alias_t tdd_alias2_t;
endpackage

module tdd_top;
  import tdd_p::*;
  tdd_alias2_t h;
`ifdef SVLSP_TEST_PROBES
     probe: h.
`endif
endmodule
