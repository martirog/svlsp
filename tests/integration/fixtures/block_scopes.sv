// block_scopes.sv -- fixture for test_44 (plan.md §6.30, "block scopes").

module bsc_m;
  int bsc_r;
  initial begin
    int bsc_x;
    bsc_x = 1;
    bsc_r = bsc_x;
  end
  initial begin
    int bsc_x;
    begin
      int bsc_x;
      bsc_x = 3;
    end
    bsc_x = 2;
  end
  function void bsc_f();
    for (int bsc_i = 0; bsc_i < 2; bsc_i++)
      bsc_r += bsc_i;
    for (int bsc_i = 0; bsc_i < 3; bsc_i++) begin
      bsc_r += bsc_i;

    end
  endfunction
endmodule
module bsc_line;
  initial begin int bsc_v; begin int bsc_v; bsc_v = 1; end bsc_v = 2; end
  function int bsc_g(int bsc_arg); return bsc_arg; endfunction int bsc_w = bsc_arg;
  initial begin int bsc_in; end 
endmodule
