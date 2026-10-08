// Negative completion visibility: a package's items reach a module only
// through an import written in that module (or re-exported), a class's
// members are never bare names outside it, and a function body can't
// declare a class or another subroutine. Probes are the blank lines.

package cvis_p;
  function int cvis_pf(); return 0; endfunction
  class CvisPC;
    function void cvis_pcm(); endfunction
  endclass
endpackage

package cvis_q;
  import cvis_p::*;
  function int cvis_qf(); return 0; endfunction
endpackage

module cvis_imp;
  import cvis_p::*;

endmodule

module cvis_none;

endmodule

module cvis_trans;
  import cvis_q::*;

endmodule

class CvisC;
  function void cvis_cf();

  endfunction
endclass
