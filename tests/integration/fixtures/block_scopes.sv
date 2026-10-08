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
