// sighelp_ctor.sv — fixture for test_12_signature_help.sh (§6.30 follow-up):
// `new(` inside shct_B's method constructs an shct_A, so signature help is
// shct_A's constructor, not the enclosing class's. The cursor is on the
// second argument.
class shct_A;
  function new(string shct_name, int shct_n = 0); endfunction
endclass
class shct_B;
  function new(bit shct_flag); endfunction
  function void mk();
    shct_A a = new("x", 1);
  endfunction
endclass
