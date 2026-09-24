// defref_top.sv — used by test_40_scoped_definition_references.sh.
// Go-to-definition through `::`, `include and `.`, and exact-location
// find-references per declaration kind. Every name is prefixed defref_
// because the Emacs suite shares one server and one DB across all tests.
`include "defref_inc.svh"
package defref_pkg_a;
  class defref_Item;
    int defref_val;
    function int defref_get(); return defref_val; endfunction
  endclass
endpackage
module defref_leaf(input logic defref_din);
endmodule
module defref_top;
  defref_pkg_a::defref_Item a;
  defref_IncCls inc;
  logic defref_sig;
  int r;
  defref_leaf u0(.defref_din(defref_sig));
  defref_leaf u1(.defref_din(defref_sig));
  initial begin
    r = a.defref_get();
    r = a.defref_val;
    inc.defref_hello();
  end
endmodule
