// completion_in_comment.sv — fixture for test_08_completion.sh: completion
// inside a comment or string literal offers nothing (prose, not code).
// Positions (LSP 0-based): line 7 char 27 is the end of "cmtc_" in the line
// comment; line 8 char 25 is right after the first "cmtc_" in the string.
module cmtc_top;
  int cmtc_value;
  // see also:
  int cmtc_other;  // cmtc_
  initial $display("cmtc_ cmtc_");
endmodule
