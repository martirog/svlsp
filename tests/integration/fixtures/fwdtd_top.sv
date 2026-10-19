// fwdtd_top.sv -- forward typedefs and declare-before-use (test_46).

package fwdtd_p;
  typedef class fwdtd_late;
  `include "fwdtd_a.svh"
  `include "fwdtd_b.svh"
endpackage
