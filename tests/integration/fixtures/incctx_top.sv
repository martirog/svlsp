// incctx_top.sv -- a package spread over `include'd files (test_45).

`define INCCTX_DECL(n) int n;
package incctx_q;
  typedef int incctx_qt;
endpackage
package incctx_p;
  import incctx_q::*;
  `include "incctx_a.svh"
  `include "incctx_b.svh"
endpackage
