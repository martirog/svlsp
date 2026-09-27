// chained_dot_completion.sv — fixture for plan.md §6.14 chained/
// function-call dot-completion (test_30_chained_dot_completion.sh).
//
// Greeter/BaseGreeter each declare one distinctly-named method so a probe
// can prove exactly which class a chain resolved through, not just that it
// resolved to *something*: Greeter has greet_child, BaseGreeter has
// greet_base. BaseFactory's get_child() returns BaseGreeter; Factory
// extends BaseFactory and *overrides* get_child() to return Greeter
// instead -- this lets the this./super. probes below distinguish "resolved
// through Factory's own override" from "resolved through the parent".
//
// "probe: ..." lines (in a never-defined `ifdef) put the dot-completion trigger text at a
// precise (line, char) position -- same trick dot_completion.sv/
// builtin_method_completion.sv use (see their own header comments for why
// the probe text sits in a never-defined `ifdef).

class Greeter;
  function void greet_child(); endfunction
endclass

class BaseGreeter;
  function void greet_base(); endfunction
endclass

class BaseFactory;
  function BaseGreeter get_child(); endfunction
endclass

class Factory extends BaseFactory;
  function Greeter get_child(); endfunction

  function void method_in_factory();
`ifdef SVLSP_TEST_PROBES
       probe: this.get_child().gr
       probe: super.get_child().gr
`endif
  endfunction
endclass

function Factory make_factory();
endfunction

function int get_num();
endfunction

module chained_dot_completion_top;
`ifdef SVLSP_TEST_PROBES
     probe: make_factory().get_child().gr
     probe: get_num().foo().bar
`endif
endmodule
