// dot_completion.sv — fixture for plan.md §6.10 dot/member-access completion
// (test_27_dot_completion.sh).
//
// DotWidget declares two members whose names deliberately share a prefix
// ("sp") so a typed-prefix scenario can distinguish them: "spi" matches only
// spin_up, not speed_val.
//
// The "probe: ..." lines put the dot-completion trigger text at a precise
// (line, char) position for dotCompletionContext to read. A never-defined
// `ifdef hides them from the parser (a comment won't do: completion offers
// nothing inside one) -- same trick fuzzy_completion.sv uses; see its header
// for the bug this avoids: bare no-paren text on a real code line gets
// mis-parsed as an implicit-type variable declaration under this grammar's
// data_type ambiguity, self-matching every probe rather than testing it.

class DotWidget;
  function void spin_up(); endfunction
  int speed_val;
endclass

module dot_completion_top;
  DotWidget w;
  int plain_int;
`ifdef SVLSP_TEST_PROBES
     probe: w.
     probe: w.spi
     probe: plain_int.
`endif
endmodule
