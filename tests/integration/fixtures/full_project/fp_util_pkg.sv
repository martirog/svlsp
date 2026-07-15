// fp_util_pkg.sv — package providing symbols for the full-project
// integration test (imported both specifically and via transitive export).
package fp_util_pkg;
    class FpWidget;
        int value;
    endclass

    function automatic int fp_compute(int x);
        return x * 2;
    endfunction
endpackage
