// sighelp_keywords.sv — fixture for test_12_signature_help.sh, plan.md §6.29
// part B: keyword-construct signature help. The cursor right after the
// second `;` of the `for (` header is on its step part.
module sh29k_loop;
  int sh29k_q[4];
  initial begin
    for (int sh29k_i = 0; sh29k_i < 4; sh29k_i++)
      sh29k_q[sh29k_i] = sh29k_i;
  end
endmodule
