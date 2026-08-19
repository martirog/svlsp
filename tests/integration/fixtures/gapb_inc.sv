// gapb_inc.sv — included file for Gap B (`__FILE__`/`__LINE__` inside
// `include`d files; see handoff.md "Gap B"). Declares a module BEFORE and
// AFTER the `__FILE__`/`__LINE__` usage: if pass-1 stripping ever shifted
// the line count of this file, gapb_after_mod would land on the wrong line.
module gapb_before_mod;
endmodule

module gapb_marker_mod;
    parameter string GAPB_FILE = `__FILE__;
    parameter int    GAPB_LINE = `__LINE__;
endmodule

module gapb_after_mod;
endmodule
