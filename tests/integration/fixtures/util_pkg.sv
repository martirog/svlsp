// util_pkg.sv — package with symbols for import-resolution tests.
package util_pkg;
    class DataItem;
        int value;
        function void reset();
        endfunction
    endclass

    class Logger;
        int level;
    endclass

    function automatic int compute(int x);
        return x * 2;
    endfunction
endpackage
