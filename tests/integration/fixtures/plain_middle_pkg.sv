// plain_middle_pkg.sv — imports base_pkg but does NOT export it; must not leak transitively.
package plain_middle_pkg;
    import base_pkg::*;

    class Gamma;
        int count;
    endclass
endpackage
