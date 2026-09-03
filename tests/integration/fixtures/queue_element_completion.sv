// queue_element_completion.sv — fixture for plan.md §6.15 queue/
// associative-array element access completion (indexing into a container
// to complete on a class-typed *element*, including multi-dimensional
// partial vs. full indexing), test_31_queue_element_completion.sh.
//
// Widget is the class-typed element throughout. `arr` is a fixed array of
// queues of Widget -- arr[i] is still a queue (partial indexing should
// offer the queue's own methods), arr[i][j] reaches the Widget element
// itself (full indexing should offer Widget's own members). `iq` is a
// plain int queue -- a regression guard that indexing into a built-in-typed
// container still yields nothing.
//
// "// probe: ..." comment lines put the dot-completion trigger text at a
// precise (line, char) position -- same trick builtin_method_completion.sv/
// chained_dot_completion.sv use (see their own header comments for why the
// probe text sits inside a comment).

class Widget;
  function void greet(); endfunction
endclass

module queue_element_completion_top;
  Widget q[$];
  Widget aa[string];
  Widget arr[4][$];
  int iq[$];

  // probe: q[0].
  // probe: aa["k"].
  // probe: arr[i].
  // probe: arr[i][j].
  // probe: iq[0].
endmodule
