// keyword_completion.sv — probe positions for context-aware keyword
// completion integration tests (test_28_keyword_completion.sh, plan.md
// §6.9 extended with legality rules).
//
// Three "// probe:" comment lines put the cursor inside a module body, a
// class body (outside any of its methods), and a function body -- same
// technique as fuzzy_completion.sv: probe text sits inside a comment so it
// is never lexed/parsed and can't create a spurious symbol of its own.
//
//   line 3  (0-based) — inside module "top"'s body, outside any nested scope
//   line 9  (0-based) — inside class "Widget"'s body, outside method_a
//   line 13 (0-based) — inside function "method_a"'s body
module top;
    // probe: module-scope
    int x;

    class Widget;
        function void method_a();
            // probe: function-scope
        endfunction
        // probe: class-scope
        int y;
    endclass
endmodule
