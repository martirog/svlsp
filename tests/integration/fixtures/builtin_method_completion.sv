// builtin_method_completion.sv — fixture for plan.md §6.13 built-in
// container/type method completion (test_29_builtin_method_completion.sh).
//
// Covers a representative subset of the built-in kinds (queue, associative
// array, mailbox, process, and a plain class with no explicitly-declared
// randomize) -- the unit-test layer already covers every kind (dynamic/
// fixed-size array, string, event, semaphore) exhaustively.
//
// "// probe: ..." comment lines put the dot-completion trigger text at a
// precise (line, char) position purely for dotCompletionContext to read --
// same trick dot_completion.sv/fuzzy_completion.sv use (see their own
// header comments for why: bare no-paren text on a real code line risks
// being mis-parsed under this grammar's documented data_type ambiguity).

class Widget;
  function void greet(); endfunction
endclass

module builtin_method_completion_top;
  int q[$];
  int aa[string];
  mailbox #(int) mbx;
  process p;
  Widget obj;

  // probe: q.
  // probe: aa.
  // probe: mbx.
  // probe: p.
  // probe: obj.
endmodule
