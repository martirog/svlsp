// middle_pkg.sv — imports base_pkg and re-exports it via `export base_pkg::*;`.
package middle_pkg;
    import base_pkg::*;
    export base_pkg::*;

    class Beta;
        int level;
    endclass
endpackage
