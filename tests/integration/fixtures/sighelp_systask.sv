// sighelp_systask.sv — fixture for test_12_signature_help.sh, plan.md §6.29
// part C: system task signature help from the static table. The cursor
// right after the second comma of `$sformatf(` (before its third argument)
// is in the variadic args tail. Kept out of ref_rename_sighelp.sv: that
// file's deliberately unfinished calls make it slow to parse in the ASan
// build, and growing it pushed its first request past the client timeout.
module sh29_systask;
  string sh29_s;
  int    sh29_a;
  initial begin
    sh29_s = $sformatf("%0d %0d", sh29_a, sh29_a);
  end
endmodule
