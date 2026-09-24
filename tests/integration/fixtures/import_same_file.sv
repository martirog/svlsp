// import_same_file.sv — a package declared and wildcard-imported in the same
// file (plan.md §6.30 step 1). sfwi_ prefix: the Emacs suite shares one DB.
package sfwi_pkg;
    class sfwi_Item;
    endclass
endpackage

package sfwi_other_pkg;
    class sfwi_Other;
    endclass
endpackage

import sfwi_pkg::*;

module sfwi_top;

    // LSP line 16 (0-based), char 4 — completion trigger inside sfwi_top.
endmodule
