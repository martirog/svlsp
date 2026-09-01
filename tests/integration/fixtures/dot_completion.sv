// dot_completion.sv — fixture for plan.md §6.10 dot/member-access completion
// (test_27_dot_completion.sh).
//
// DotWidget declares two members whose names deliberately share a prefix
// ("sp") so a typed-prefix scenario can distinguish them: "spi" matches only
// spin_up, not speed_val.
//
// The "// probe: ..." comment lines put the dot-completion trigger text at a
// precise (line, char) position purely for dotCompletionContext to read.
// Being inside a comment, the text is never lexed by the parser -- same
// trick fuzzy_completion.sv uses (see its own header comment for the bug
// this avoids: bare no-paren text on a real code line gets mis-parsed as an
// implicit-type variable declaration under this grammar's data_type
// ambiguity, self-matching every probe rather than testing anything).

class DotWidget;
  function void spin_up(); endfunction
  int speed_val;
endclass

module dot_completion_top;
  DotWidget w;
  int plain_int;

  // probe: w.
  // probe: w.spi
  // probe: plain_int.
endmodule
