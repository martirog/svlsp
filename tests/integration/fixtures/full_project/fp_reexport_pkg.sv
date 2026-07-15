// fp_reexport_pkg.sv — imports and re-exports fp_util_pkg::*, and declares
// its own class. fp_top.sv imports only this package (wildcard); fp_util_pkg's
// FpWidget must still be visible there via the export chain.
package fp_reexport_pkg;
    import fp_util_pkg::*;
    export fp_util_pkg::*;

    class FpExtra;
        int level;
    endclass
endpackage
