// live_edit_completion.sv — fixture for test_32_live_edit_completion.sh.
//
// Unlike every other completion fixture (dot_completion.sv,
// builtin_method_completion.sv, chained_dot_completion.sv,
// queue_element_completion.sv, ...), the dot-completion trigger text here
// is never present in the file on disk — it's typed into the live buffer
// via a real textDocument/didChange edit (lsp-mode's own after-change
// hook), exercising the debounced recompile path (plan.md §6.8) the way
// an actual editing session does, not just a completion request against
// content baked into the initial didOpen.
//
// Widget declares two distinctly-named methods (greet/wave) so a "gr"
// prefix typed live can prove both that the right member shows up *and*
// that prefix filtering still works on freshly-typed, never-before-parsed
// text. The blank line before "endmodule" is where each test case inserts
// its own new text.

class Widget;
  function void greet(); endfunction
  function void wave(); endfunction
endclass

module live_edit_completion_top;
  Widget w;

  // INSERT-EDIT-HERE

endmodule
