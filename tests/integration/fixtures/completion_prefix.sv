// completion_prefix.sv — completion with the cursor at the start or in the
// middle of an existing word (test_48_completion_prefix.sh, plan.md §6.33
// F6/C7): only the text before the cursor is the prefix.
class Pt;
  int alpha_m;
  int beta_m;
endclass

module top;
  int alpha;
  int beta;
  Pt  p;
  initial begin
    alpha = 1;
    p.alpha_m = 2;
  end
endmodule
