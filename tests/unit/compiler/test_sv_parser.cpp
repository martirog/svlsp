#include <catch2/catch_test_macros.hpp>
#include <antlr4-runtime.h>
#include "SvLexer.h"
#include "SvParser.h"
#include <fstream>
#include <sstream>

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    REQUIRE(f.is_open());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int parseErrors(const std::string& src, bool sll = false) {
    antlr4::ANTLRInputStream input(src);
    SvLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    SvParser parser(&tokens);
    lexer.removeErrorListeners();
    parser.removeErrorListeners();
    if (sll)
        parser.getInterpreter<antlr4::atn::ParserATNSimulator>()->setPredictionMode(
            antlr4::atn::PredictionMode::SLL);
    parser.source_text();
    return static_cast<int>(parser.getNumberOfSyntaxErrors());
}

TEST_CASE("SvParser parses module_basic.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/module_basic.sv")) == 0);
}

TEST_CASE("SvParser parses module_params.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/module_params.sv")) == 0);
}

TEST_CASE("SvParser parse tree root is non-null for valid input", "[compiler][parser]") {
    std::string src = "module top; endmodule\n";
    antlr4::ANTLRInputStream input(src);
    SvLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    SvParser parser(&tokens);
    lexer.removeErrorListeners();
    parser.removeErrorListeners();

    antlr4::tree::ParseTree* tree = parser.source_text();
    REQUIRE(tree != nullptr);
    REQUIRE(parser.getNumberOfSyntaxErrors() == 0);
}

TEST_CASE("SvParser reports errors for malformed input", "[compiler][parser]") {
    // Brace is not valid SV module syntax
    REQUIRE(parseErrors("module bad { endmodule\n") > 0);
}

TEST_CASE("SvParser parses interfaces.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/interfaces.sv")) == 0);
}

TEST_CASE("SvParser parses always_blocks.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/always_blocks.sv")) == 0);
}

TEST_CASE("SvParser parses functions_tasks.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/functions_tasks.sv")) == 0);
}

TEST_CASE("SvParser parses classes.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/classes.sv")) == 0);
}

TEST_CASE("SvParser parses packages.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/packages.sv")) == 0);
}

TEST_CASE("SvParser parses structs_unions.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/structs_unions.sv")) == 0);
}

TEST_CASE("SvParser parses enums.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/enums.sv")) == 0);
}

TEST_CASE("SvParser parses generate.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/generate.sv")) == 0);
}

TEST_CASE("SvParser parses assertions.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/assertions.sv")) == 0);
}

TEST_CASE("SvParser parses clocking.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/clocking.sv")) == 0);
}

TEST_CASE("SvParser parses coverage.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/coverage.sv")) == 0);
}

TEST_CASE("SvParser parses constraints.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/constraints.sv")) == 0);
}

TEST_CASE("SvParser parses macros.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/macros.sv")) == 0);
}

TEST_CASE("SvParser parses timescale.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/timescale.sv")) == 0);
}

TEST_CASE("SvParser parses bind.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/bind.sv")) == 0);
}

TEST_CASE("SvParser parses program.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/program.sv")) == 0);
}

TEST_CASE("SvParser parses checker.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/checker.sv")) == 0);
}

TEST_CASE("SvParser parses dpi.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/dpi.sv")) == 0);
}

TEST_CASE("SvParser parses a string literal containing an escaped quote", "[compiler][parser][stringescape]") {
    // Regression test for the STRING_LITERAL lexer rule not handling `\"`
    // (found via the real UVM corpus: `reg/uvm_vreg.svh:435`,
    // `` `uvm_error("RegModel", $sformatf("Virtual register \"%s\" cannot
    // have 0 bits", name)) `` -- pattern reproduced here without the macro
    // layer, since STRING_LITERAL is a pure lexer rule).
    std::string src =
        "module top;\n"
        "  initial $display(\"Virtual register \\\"%s\\\" cannot have 0 bits\", \"x\");\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a string literal containing an escaped backslash", "[compiler][parser][stringescape]") {
    std::string src =
        "module top;\n"
        "  initial $display(\"a\\\\b\");\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser does not let an escaped quote swallow the rest of the line", "[compiler][parser][stringescape]") {
    // Mirrors the real cascade pattern from `base/uvm_root.svh:600`: a
    // concatenation expression with plain (non-escaped) string arguments
    // following one that itself contains an escaped quote -- if the lexer
    // mis-terminates the first string early, everything after it
    // mis-tokenizes and the whole statement fails to parse.
    std::string src =
        "module top;\n"
        "  string test_name;\n"
        "  initial $display({\"before \\\"quoted\\\" after \", test_name, \"...\"});\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still reports an error for a truly unterminated string", "[compiler][parser][stringescape]") {
    // Guards against a too-permissive fix (e.g. accidentally consuming to EOF).
    REQUIRE(parseErrors("module top; initial $display(\"unterminated); endmodule\n") > 0);
}

TEST_CASE("SvParser parses the LRM-correct void'(...) cast form", "[compiler][parser][voidcast]") {
    // Regression test for subroutine_call_statement previously requiring
    // 'void' '(' ... ')' ';' with no SINGLE_QUOTE -- real UVM uses the
    // LRM-correct `void'(...)` form pervasively (e.g. base/uvm_root.svh:916,
    // `void'($sscanf(timeout,"%d,%s",timeout_int,override_spec));`).
    std::string src =
        "module top;\n"
        "  function int f(); return 0; endfunction\n"
        "  initial void'(f());\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses the void(...) workaround form", "[compiler][parser][voidcast]") {
    // Guards against the SINGLE_QUOTE? fix accidentally making it required.
    std::string src =
        "module top;\n"
        "  function int f(); return 0; endfunction\n"
        "  initial void(f());\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a class-scoped chained method call as an assignment RHS",
          "[compiler][parser][classscopedcall]") {
    // Regression test for ps_or_hierarchical_tf_identifier previously having no
    // class_scope alternative -- real UVM uses this pervasively for the factory
    // idiom, e.g. reg/uvm_mem.svh:1151, `rw = uvm_reg_item::type_id::create("s")`.
    std::string src =
        "class C; function void f(); "
        "uvm_reg_item rw; "
        "rw = uvm_reg_item::type_id::create(\"s\"); "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a parameterized class-scoped call",
          "[compiler][parser][classscopedcall]") {
    // Mirrors reg/sequences/uvm_reg_mem_shared_access_seq.svh:99,
    // `uvm_resource_db#(bit)::get_by_name(...)`.
    std::string src =
        "class C; function void f(); "
        "bit x; "
        "x = uvm_resource_db#(bit)::get_by_name(\"s\", \"n\", 0); "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a class-scoped call with an elided middle argument",
          "[compiler][parser][classscopedcall]") {
    // Mirrors the exact real shape at reg/uvm_mem.svh:1151:
    // `rw = uvm_reg_item::type_id::create("s",,get_full_name());` -- confirms
    // the elided-argument compounding seen in the real diagnostics was the same
    // root cause as the plain case above, not a second bug.
    std::string src =
        "class C; function void f(); "
        "uvm_reg_item rw; "
        "rw = uvm_reg_item::type_id::create(\"s\",,get_full_name()); "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a class-scoped call used as a bare statement",
          "[compiler][parser][classscopedcall]") {
    // Proves the fix covers subroutine_call_statement (via subroutine_call ->
    // tf_call), not just expression position.
    std::string src =
        "class C; function void f(); "
        "uvm_reg_item::type_id::create(\"s\"); "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses non-call class-scoped field access",
          "[compiler][parser][classscopedcall]") {
    // Guards against the new class_scope alternative in
    // ps_or_hierarchical_tf_identifier stealing a case that
    // `class_qualifier hierarchical_identifier select` (primary) already
    // handled correctly for non-call scoped field/property access.
    std::string src =
        "class C; function void f(); "
        "int x; "
        "x = A::B::my_static_field; "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses a plain non-scoped function call",
          "[compiler][parser][classscopedcall]") {
    // Guards against ps_or_hierarchical_tf_identifier's existing alternatives
    // (package_scope? tf_identifier | hierarchical_tf_identifier) regressing.
    std::string src =
        "class C; function void f(); "
        "int x; "
        "x = foo(\"s\"); "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a bare #0; zero-delay control statement",
          "[compiler][parser][delayzero]") {
    // Regression test: deferred_immediate_{assert,assume,cover}_statement used
    // to spell their special delay as the raw literal '#0', which ANTLR turns
    // into its own implicit lexer token -- so the two characters #0 could
    // *never* lex as '#' + DECIMAL_NUMBER, breaking the extremely common
    // zero-delay "absorb a delta cycle" idiom (e.g. base/uvm_barrier.svh:209,
    // `#0; //this process was last to wait; allow other procs to resume first`).
    std::string src = "module top; initial begin #0; end endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses deferred immediate assert #0 (...)",
          "[compiler][parser][delayzero]") {
    // Guards against the '#0' -> '#' DECIMAL_NUMBER fix breaking the
    // construct that literal existed for in the first place.
    std::string src =
        "module top; initial begin assert #0 (1) else $error(\"x\"); end endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses a non-zero delay control statement",
          "[compiler][parser][delayzero]") {
    // Guards against the fix accidentally requiring the delay to be zero.
    std::string src = "module top; initial begin #5; end endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a method literally named 'sample'",
          "[compiler][parser][coveragesample]") {
    // Regression test: coverage_event's `with function sample(...)` alternative
    // used the raw literal 'sample', making it an implicit reserved keyword
    // everywhere -- blocking the standard UVM reg/mem functional-coverage
    // callback name (e.g. reg/uvm_mem.svh:505,
    // `protected virtual function void sample(...); endfunction`).
    std::string src = "class C; function void sample(int x); endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses the 'with function sample(...)' coverage-event form",
          "[compiler][parser][coveragesample]") {
    // Guards against the 'sample' -> IDENTIFIER fix breaking the construct
    // that literal existed for in the first place.
    std::string src =
        "class C; int x; covergroup cg with function sample(int y); "
        "coverpoint x; endgroup endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses an empty assignment pattern '{}",
          "[compiler][parser][emptypattern]") {
    // Regression test: assignment_pattern required at least one element inside
    // '{ ... }; SV allows an empty one for empty queues/dynamic arrays (e.g.
    // base/uvm_lru_cache.svh:206, `return '{};` for an empty int q[$]).
    std::string src =
        "class C; function void f(); int q[$]; q = '{}; endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses a non-empty assignment pattern",
          "[compiler][parser][emptypattern]") {
    // Guards against the new empty alternative breaking the general case.
    std::string src =
        "class C; function void f(); int q[3]; q = '{1, 2, 3}; endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a const class property initialized with a general expression",
          "[compiler][parser][constinitexpr]") {
    // Regression test: class_property's `const` alternative restricted the
    // initializer to constant_expression, excluding new(...) and other general
    // runtime expressions -- only parameter/localparam require a true
    // compile-time constant. With a qualifier after `const`
    // (base/uvm_transaction.svh:443, `const local uvm_event_pool events =
    // new("events");`) only class_property's `const` alternative applies,
    // which takes a class_new too since plan.md §6.33 C2.
    std::string src =
        "class E; function new(string s=\"\"); endfunction endclass\n"
        "class C; const E h = new(\"x\"); const local E e = new(\"e\"); endclass\n";
    REQUIRE(parseErrors(src) == 0);
    REQUIRE(parseErrors(src, true) == 0);
}

TEST_CASE("SvParser still parses a const class property with a constant_expression initializer",
          "[compiler][parser][constinitexpr]") {
    // Guards against the constant_expression -> expression widening regressing
    // the already-working case.
    std::string src = "class C; const local int x = 0; endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a class method literally named randomize()",
          "[compiler][parser][randomize]") {
    // Regression test: randomize_call used the raw literal 'randomize',
    // making it an implicit reserved keyword everywhere (same shape as the
    // already-fixed coverage_event/'sample' collision above) and blocking any
    // method actually named randomize() -- a common, legal UVM idiom, since
    // every class implicitly gets a randomize() method and overriding it is
    // normal. Fixed by loosening randomize_call's own keyword to IDENTIFIER.
    std::string src = "class C; function void randomize(); endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses ordinary randomize()/std::randomize() calls",
          "[compiler][parser][randomize]") {
    // Guards against the 'randomize' -> IDENTIFIER loosening regressing the
    // already-working call-site shapes.
    std::string src =
        "class C; function void f(); "
        "void'(randomize()); "
        "void'(std::randomize()); "
        "void'(randomize() with { 1; }); "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses super.new(...) after a preceding statement in a constructor",
          "[compiler][parser][superctor]") {
    // Regression test: class_constructor_declaration hardcoded the LRM's
    // strict structural position, only allowing super.new(...) as the
    // literal first thing after any declarations -- real code (and real
    // simulators) commonly place statements, e.g. argument validation,
    // before it. Fixed by allowing super.new(...) anywhere among the
    // constructor's own statements, not just first.
    std::string src =
        "class Base; function new(string name); endfunction endclass\n"
        "class C extends Base; "
        "function new(string name); "
        "if (name == \"\") begin end "
        "super.new(name); "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses super.new(...) as a constructor's first statement",
          "[compiler][parser][superctor]") {
    // Guards against the position-relaxation regressing the already-working,
    // LRM-strict placement.
    std::string src =
        "class Base; function new(string name); endfunction endclass\n"
        "class C extends Base; "
        "function new(string name); "
        "super.new(name); "
        "endfunction endclass\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses the constructs SLL prediction used to misparse, under SLL too",
          "[compiler][parser][sll]") {
    // plan.md §6.33 C2: SLL prediction can't see a rule's caller, so where the
    // grammar decided by context (a `.` loop that must stop before a call's
    // parens, a `::` loop that must stop before a class scope's last `::`,
    // `9200s` read as a number) it reported errors LL didn't -- 19,348 on
    // UVM. Each line here is one such shape (the original grammar: 31 SLL
    // errors on this source, 0 LL). The first `x = a.b + 1;` is a block's
    // leading statement, which the grammar also reads as a declaration.
    const std::string src =
        "package pkg; function int g(int a); return a; endfunction endpackage\n"
        "class C;\n"
        "  function void f();\n"
        "    x = a.b + 1;\n"
        "    $display(\"x\");\n"
        "    if (re.len() == 0) x = 1;\n"
        "    x = a.b + 1;\n"
        "    x = a.b[i] + a.c[i];\n"
        "    this.srandom(1);\n"
        "    map[e.name()] = e;\n"
        "    x = m[g(1)] + m[a.b(2)] + m[pkg::g(3)];\n"
        "    if ($time == 9200s) x = 1;\n"
        "    x = uvm_pkg::UVM_NONE;\n"
        "    g(x).h();\n"
        "    a.b(x).c(y);\n"
        "    x = D#(int)::g(1);\n"
        "    D::E::g();\n"
        "    void'(obj.randomize() with { a < 3; });\n"
        "    x = arr.sum() with (item * 2);\n"
        "    t;\n"
        "  endfunction\n"
        "  uvm_pkg::uvm_report_object o;\n"
        "  uvm_config_db#(uvm_object) db;\n"
        "endclass\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("SvParser accepts every call statement form, and still rejects non-call statements",
          "[compiler][parser][sll]") {
    // subroutine_call spells out the `.method` chain per callee form instead
    // of `primary '.' method_call_body` (plan.md §6.33 C2); a bare primary
    // there would have made `5;` a statement.
    auto wrap = [](const std::string& stmt) {
        return "module m; initial begin " + stmt + " end endmodule\n";
    };
    for (const std::string stmt :
         {"t;", "x.y;", "f(1);", "f(1).g();", "a.b(1).c(2);", "a[1].b();", "this.m(1);",
          "super.m();", "pkg::f(1).g;", "C#(int)::f(1);", "x.randomize() with {a < 1;};",
          "std::randomize(x);", "void'(a.b(1));", "$display(1);"}) {
        INFO(stmt);
        CHECK(parseErrors(wrap(stmt)) == 0);
        CHECK(parseErrors(wrap(stmt), true) == 0);
    }
    for (const std::string stmt : {"5;", "\"s\";", "{a, b};"}) {
        INFO(stmt);
        CHECK(parseErrors(wrap(stmt)) > 0);
    }
}
