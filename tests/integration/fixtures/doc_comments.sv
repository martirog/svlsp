// Logs a dcmt message.
`define DCMT_LOG(msg) $display(msg)
// A documented class.
// Second line.
class dcmt_c;
  // How many items.
  int dcmt_count;
  // Adds two numbers.
  function int dcmt_add(int a, int b);
    return a + b;
  endfunction
  function void dcmt_run();
    dcmt_add(1, 2);
    `DCMT_LOG("x");
  endfunction
endclass
`ifdef SVLSP_TEST_PROBES
  dcmt_
`endif
