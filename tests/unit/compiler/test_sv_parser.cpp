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

// plan.md §6.39 G1-G5: constructs ibex uses that simulators accept. Each is
// checked under LL and SLL (the two stages of §6.33 C2).

TEST_CASE("G1: a cross body's bins items end in one semicolon each",
          "[compiler][parser][ibex]") {
    const std::string src =
        "module m(input logic clk); logic [1:0] a; logic e;\n"
        "  covergroup cg @(posedge clk);\n"
        "    cp_a : coverpoint a; cp_e : coverpoint e;\n"
        "    x : cross cp_a, cp_e iff (e) {\n"
        "      option.weight = 2;\n"
        "      illegal_bins i1 = binsof(cp_a) intersect {2'b00};\n"
        "      ignore_bins i2 = binsof(cp_a) intersect {1} && binsof(cp_e) intersect {1'b1};\n"
        "      bins b3 = binsof(cp_e);\n"
        "      function CrossQueueType f(); endfunction\n"
        "    }\n"
        "    y : cross cp_a, cp_e { }\n"
        "    z : cross cp_a, u_core.st.mie iff\n"
        "          (e | e) {\n"
        "      illegal_bins i4 = (!binsof(u_core.st.mie) intersect {1, 2} &&\n"
        "                         binsof(cp_a) intersect {0}) with (cp_a > 0);\n"
        "    }\n"
        "  endgroup\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("G2: a queue slice may end at $", "[compiler][parser][ibex]") {
    const std::string src =
        "module m;\n"
        "  string s[$], r[$]; int q[$];\n"
        "  initial begin\n"
        "    r = s[1:$];\n"
        "    q = q[$-1:$];\n"
        "    q = q[0:$-2];\n"
        "    q[$] = 1;\n"
        "  end\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("G3: a bind directive at file level", "[compiler][parser][ibex]") {
    const std::string src =
        "interface f_if; logic a; endinterface\n"
        "module f_core; logic a; endmodule\n"
        "bind f_core f_if u_bind (.*);\n"
        "bind f_core f_if u_b2 (.a(a));\n"
        "bind f_core : u_core1, u_core2 f_if #(.P(1)) u_b3 (.*);\n"
        "module top; bind f_core f_if u_b4 (.*); endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("G4: whitespace between a number's size, base and value",
          "[compiler][parser][ibex]") {
    const std::string src =
        "module m;\n"
        "  logic b = 1'b 0;\n"
        "  logic [7:0] h = 8'h FF, o = 8 'o 17, d = 8'd 255, s = 8'sh 7f;\n"
        "  logic u = 'b 1;\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("G5: foreach over a[i][j] as well as a[i, j]", "[compiler][parser][ibex]") {
    const std::string src =
        "module m; int d[2][3]; int e[2][3][4];\n"
        "  initial begin\n"
        "    foreach (d[i][j]) d[i][j] = 0;\n"
        "    foreach (d[i, j]) d[i][j] = 0;\n"
        "    foreach (e[i][j][k]) e[i][j][k] = 0;\n"
        "    foreach (e[, j]) e[0][j][0] = 1;\n"
        "  end\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("untyped and typed ports parse under SLL", "[compiler][parser][sll][ibex]") {
    // plan.md §6.39: `input clk_i,` made SLL take `clk_i` as a type, so
    // every RTL module with an untyped port was parsed twice (SLL, then LL).
    const std::string src =
        "typedef logic [3:0] mask_t;\n"
        "module m #(parameter mask_t P = {$bits(mask_t){1'b1}}) (\n"
        "  input clk_i,\n"
        "  input wire rst_ni,\n"
        "  input signed [3:0] s,\n"
        "  input [3:0] v,\n"
        "  input mask_t mk,\n"
        "  output logic o,\n"
        "  inout x\n"
        ");\n"
        "endmodule\n"
        "module n (a, b, c);\n"
        "  input a, b;\n"
        "  output [1:0] c;\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("generate items and continuous assigns keep every IEEE form",
          "[compiler][parser][sll]") {
    // plan.md §6.39: generate_item and continuous_assign were rewritten
    // without overlapping alternatives (SLL scanned each item to its end).
    const std::string src =
        "interface ifc; extern function void f(); endinterface\n"
        "module m(input logic clk); wire w, w2; logic v; logic [3:0] q;\n"
        "  for (genvar i = 0; i < 2; i++) begin : g\n"
        "    always_ff @(posedge clk) v <= w;\n"
        "    assign w = v;\n"
        "    initial $display(\"%0d\", i);\n"
        "    sub u (.a(w));\n"
        "    and g1 (w2, w, v);\n"
        "    if (i == 0) begin : g_if assign q[0] = 1'b1; end\n"
        "    covergroup cg @(posedge clk); cp : coverpoint v; endgroup\n"
        "    function automatic int f(int a); return a; endfunction\n"
        "    property p; @(posedge clk) v |-> w; endproperty\n"
        "    assert property (p);\n"
        "  end\n"
        "  assign (strong0, weak1) #(1, 2) w2 = v;\n"
        "  assign #3 w = v, w2 = !v;\n"
        "  assign {q[1], q[2]} = 2'b01;\n"
        "  assign q[3] = w;\n"
        "endmodule\n"
        "module sub(input a); endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("a cycle delay before a name is not a time literal under SLL",
          "[compiler][parser][sll][ibex]") {
    // plan.md §6.39: time_literal was `DECIMAL_NUMBER time_unit` with any
    // identifier as the unit, so SLL read `##1 a && b` as `##(1a) && b` and
    // fell back to LL (prim_lfsr in ibex). A time literal is one token now:
    // the unit follows the number without a space (IEEE 1800-2017 5.8).
    const std::string src =
        "module m(input logic clk_i, a, b, c);\n"
        "  timeunit 1ns; timeprecision 10ps;\n"
        "  logic q;\n"
        "  assert property (@(posedge clk_i) ##1 a && !b |=> c);\n"
        "  assert property (##1 a |=> c);\n"
        "  assert property (@(posedge clk_i) a ##2 b ##[1:3] c);\n"
        "  assert property (@(posedge clk_i) ##1 (a) |-> ##1 b);\n"
        "  clocking cb @(posedge clk_i); default input #1step output #2ns; endclocking\n"
        "  initial begin\n"
        "    #1 q = a;\n"
        "    #1.5ns q = b;\n"
        "    #10us;\n"
        "    if ($time == 9200s) q = 1;\n"
        "    #(2ps) q = c;\n"
        "  end\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("dotted names, selects and method chains parse under LL and SLL",
          "[compiler][parser][sll]") {
    // plan.md §6.39 cold prediction: `a.b` was both a dotted
    // hierarchical_identifier and `a` with a `.b` from select, from
    // `primary '.' method_call_body`, from net_lvalue's constant_select, from
    // hierarchical_btf_identifier and from sequence_method_call, so SLL read
    // on to the end of the statement and beyond at every dotted name. A
    // name's dots are the hierarchical_identifier's alone now; a method
    // chain follows call parens, and `.unique/.and/.or/.xor` (keywords) get
    // their own tail.
    const std::string src =
        "class C;\n"
        "  int q[$]; int a[4]; C o; int x;\n"
        "  function C g(int i); return this; endfunction\n"
        "  function void f();\n"
        "    x = o.o.x + o.a[1] + a[2] + o.o.a[x + 1];\n"
        "    x = q[$].x + q[$] + o.a[1].x;\n"
        "    x = q.size() + o.q.sum() with (item * 2) + q.sum with (item);\n"
        "    x = a.xor() ^ a.and with (item > 1) ^ a.or;\n"
        "    q = q.unique();\n"
        "    x = g(1).x + g(1).g(2).x + o.g(1).g(2).x;\n"
        "    x = $root.top.x + this.x + this.o.g(1).x;\n"
        "    x = $urandom() + $sformatf(\"%0d\", x).len() + (o).x;\n"
        "    x = C'(o).x + int'(x) + string'(x).len();\n"
        "    x = \"abc\".len() + {a[0], a[1]}.sum() + '{1, 2}.size() + {>>{a}}.size();\n"
        "    x = q[0:1].size() + a[1:0].sum() + q.find_first with (item > 1).size();\n"
        "    x = std::randomize(x) + o.randomize() + o.randomize() with { x < 3; };\n"
        "    void'(o.randomize(x) with (x) { x > 1; });\n"
        "    o.o.x = 1; o.a[1] = 2; a[1:0] = '{1, 2}; q[$].x = 3;\n"
        "    o.g(1).g(2); g(1).f(); this.f(); super.f(); o.o.f; f;\n"
        "    x = pkg::v.x + pkg::f(1).x + C::x;\n"
        "  endfunction\n"
        "endclass\n"
        "module m(input logic clk); wire [3:0] w; logic [1:0] s;\n"
        "  sub u();\n"
        "  assign u.w[1] = 1'b0;\n"
        "  assign w[1:0] = u.s;\n"
        "  sequence sq; @(posedge clk) s[0]; endsequence\n"
        "  initial wait (sq.triggered);\n"
        "  always @(posedge clk) s <= {u.s[0], w[3]};\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("system calls and cycle delays keep every form under LL and SLL",
          "[compiler][parser][sll]") {
    // plan.md §6.39 cold prediction: system_tf_call had three alternatives
    // sharing `$name (`, and `##` took any constant_primary, so after `$x`
    // a `(` could also start the sequence after `##$x` -- SLL read each
    // `$cast(...)` to the end of its block. One argument list now (each an
    // expression, a type, a clocking event or a named argument), and `##`
    // takes a number, a (package-scoped) name or a parenthesized expression.
    const std::string src =
        "package pkg; localparam int D = 2; endpackage\n"
        "module m #(parameter int N = 1) (input logic clk, a, b, en);\n"
        "  typedef logic [3:0] nib_t; nib_t v; int x; string s; int q[$];\n"
        "  initial begin\n"
        "    x = $bits(v) + $bits(logic [3:0]) + $bits(nib_t) + $size(q, 1);\n"
        "    s = $typename(int); s = $sformatf(\"%0d %s\", x, s);\n"
        "    x = $countones({a, b}) + $clog2(N + 1) + $urandom_range(1, 4);\n"
        "    if (!$cast(x, v)) $fatal(1, \"cast\");\n"
        "    $display(\"%0d\", x,, a); $display; $finish; x = $time;\n"
        "    if ($isunknown(a)) $info(\"x\");\n"
        "    $dumpvars(0, m);\n"
        "  end\n"
        "  assert property (@(posedge clk) $rose(a) |-> ##1 $stable(b));\n"
        "  assert property (@(posedge clk) a |=> $past(b, 2, en, @(posedge clk)));\n"
        "  assert property (@(posedge clk) a |=> $past(b,,, @(posedge clk)));\n"
        "  assert property (@(posedge clk) $rose(a, @(posedge clk)) |-> b);\n"
        "  assert property (@(posedge clk) a ##0 b ##N a ##(N + 1) b ##pkg::D a);\n"
        "  assert property (@(posedge clk) a ##[1:3] b ##[*] a ##[+] b ##[0:$] a);\n"
        "  assert property (@(posedge clk) ##1 (a ##1 b) |-> ##2 (a && b));\n"
        "  assert property (@(posedge clk) a ##4'd2 b);\n"
        "  default clocking cb @(posedge clk); endclocking\n"
        "  initial begin ##1; ##N; ##(N * 2); end\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("if / else-if chains, dangling else and action blocks parse under LL and SLL",
          "[compiler][parser][sll]") {
    // plan.md §6.39 cold prediction: `else if (c) s` was both a turn of
    // conditional_statement's else-if loop and `else` + a nested if, so SLL
    // read each chain to its end. The loop is gone (a nested if covers it).
    const std::string src =
        "module m(input logic clk, a, b, c); int x; logic [1:0] s;\n"
        "  always_comb begin\n"
        "    if (a) x = 1; else if (b) x = 2; else if (c) x = 3; else x = 4;\n"
        "    if (a) begin x = 1; end else if (b) begin x = 2; end else begin x = 3; end\n"
        "    if (a) if (b) x = 1; else x = 2;\n"
        "    if (a) begin if (b) x = 1; end else x = 2;\n"
        "    unique if (s == 0) x = 0; else if (s == 1) x = 1; else x = 2;\n"
        "    priority if (a) x = 0; else if (b) x = 1;\n"
        "    unique0 if (a) x = 0;\n"
        "    if (a) ; else ;\n"
        "    if (a &&& b) x = 1; else x = 0;\n"
        "  end\n"
        "  always @(posedge clk) begin\n"
        "    assert (a) else $error(\"a\");\n"
        "    assert (a) x <= 1; else x <= 2;\n"
        "    assert (a) if (b) x <= 1; else x <= 2; else $error(\"a\");\n"
        "    assume (b) else $warning(\"b\");\n"
        "    if (a) assert (b) else $error(\"b\"); else x <= 3;\n"
        "  end\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("net declarations with delays and parameterized class types parse under LL and SLL",
          "[compiler][parser][sll]") {
    // plan.md §6.39 cold prediction: net_declaration's `data_type_or_implicit
    // delay3?` let a `#` follow any data_type, so in `C #(T) x;` and
    // `virtual ifc #(N) vif;` SLL kept "C is a type, #(T) a net delay" alive
    // and read on through the rest of the package. Nets get their own data
    // type rule (net types are integral, IEEE 1800-2017 6.7.1: no class
    // types, no virtual interfaces), so a `#` follows only that.
    const std::string src =
        "package pkg; typedef logic [3:0] nib_t; endpackage\n"
        "interface ifc #(parameter int N = 1); logic [N-1:0] d; modport mp(input d); endinterface\n"
        "module m;\n"
        "  typedef logic [1:0] t2;\n"
        "  wire a; wire #2 b; wire [3:0] #(1, 2) c; wire logic [3:0] #2 d;\n"
        "  wire signed [7:0] e = 0; tri t2 #1 f; wire pkg::nib_t g; wire t2 h, i;\n"
        "  wire struct packed { logic x; logic y; } s;\n"
        "  wire logic [1:0] #(1:2:3) j; trireg (small) #4 k; wire (strong0, weak1) l = 1'b0;\n"
        "  wand #(1, 2, 3) n; interconnect [3:0] ic;\n"
        "  assign #3 a = b;\n"
        "endmodule\n"
        "class C #(type T = int); T v; endclass\n"
        "class D;\n"
        "  C #(int) c1; C#(D) c2; C #(.T(int)) c3;\n"
        "  virtual ifc #(4) vif; virtual ifc #(.N(2)).mp vmp; virtual ifc vif2;\n"
        "  typedef C #(byte) cb_t;\n"
        "  function void f(); c1 = C#(int)::new(); endfunction\n"
        "endclass\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("casts and calls in every primary form parse under LL and SLL",
          "[compiler][parser][sll]") {
    // plan.md §6.39 cold prediction: a call was both a tf_call and a name with
    // call parens, and a cast's `restricted_constant_primary '(...)` took
    // dotted, selected names, so SLL read every call to its closing paren and
    // every name to its end. Call parens on a name are a dotted name's only;
    // a cast is a tail of what it casts by.
    const std::string src =
        "package pkg; typedef logic [3:0] nib_t; typedef enum {A, B} e_t; endpackage\n"
        "class C; typedef int t; static int s; function int g(); return 1; endfunction endclass\n"
        "module m #(parameter int N = 4); typedef logic [7:0] byte_t; int x; logic [7:0] v; C o;\n"
        "  initial begin\n"
        "    x = int'(v) + byte_t'(x) + pkg::nib_t'(x) + N'(x) + 8'(x) + (N + 1)'(x) + $bits(v)'(x);\n"
        "    x = signed'(v) + unsigned'(v) + string'(x).len() + logic'(x) + real'(x) + const'(x);\n"
        "    x = pkg::e_t'(1) + C::t'(x) + type(x)'(v) + shortint'(x) + {v}'(x);\n"
        "    v = byte_t'({4'h1, 4'h2});\n"
        "    x = int'(o.g()) + o.g() + C::s + pkg::A + this_is_a_name[1] + v[3:0];\n"
        "  end\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("block declarations and leading statements parse under LL and SLL",
          "[compiler][parser][sll]") {
    // plan.md §6.39 cold prediction: data_declaration took an implicit type
    // without `var`, so a block's `x = 1;` and `t;` were also declarations
    // and SLL read on to decide. An implicit type needs `var` (IEEE
    // 1800-2017 A.2.1.3, footnote 11); and mintypmax_expression shares its
    // first expression.
    const std::string src =
        "package pkg; int v; function automatic void f(); endfunction endpackage\n"
        "class C; static function void sf(); endfunction rand int r; int unsigned u; endclass\n"
        "module m; typedef logic [3:0] nib_t; int x; nib_t n; C c;\n"
        "  task automatic t(); endtask\n"
        "  initial begin\n"
        "    int i = 0; var y; var [3:0] z = 1; automatic int k; static logic s;\n"
        "    const int ci = 1; nib_t w; nib_t [1:0] ww; C #() c2; pkg::v = 1;\n"
        "  end\n"
        "  initial begin\n"
        "    x = 1; t; c.r = 2; x++; C::sf(); pkg::f(); pkg::v = 3; n[1] = 1'b0;\n"
        "  end\n"
        "  initial begin : named int q; q = 1; t(); end\n"
        "  initial #(1:2:3) x = (1:2:3);\n"
        "  function automatic int g(int a); int b; b = a; return b; endfunction\n"
        "endmodule\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}

TEST_CASE("property implications parse under LL and SLL",
          "[compiler][parser][sll]") {
    // plan.md §6.39 cold prediction: a bare sequence and a sequence followed
    // by `|->`, `|=>` or `#=#` were separate property_expr alternatives, so
    // SLL read each antecedent to its end before choosing. They share one
    // alternative now, which also adds the followed-by `#-#` (IEEE 1800-2017
    // 16.12.9). Formal port types are optional in the port items rather
    // than empty in data_type_or_implicit, so `p1(x); @(...)` is not read
    // as a type `x` under SLL.
    const std::string src =
        "module m(input logic clk, rst, a, b, c);\n"
        "  sequence s1; a ##1 b; endsequence\n"
        "  property p1(x); @(posedge clk) x |-> ##[1:3] b; endproperty\n"
        "  a1: assert property (@(posedge clk) disable iff (rst) a |-> b);\n"
        "  a2: assert property (@(posedge clk) a ##1 b |=> c) else $error(\"x\");\n"
        "  a3: assert property (@(posedge clk) s1 #-# c);\n"
        "  a4: assert property (@(posedge clk) s1 #=# c);\n"
        "  a5: assert property (@(posedge clk) a |-> b |=> c);\n"
        "  a6: assert property (@(posedge clk) (a |-> b) and (b |=> c));\n"
        "  a7: assert property (@(posedge clk) a |-> (b or c) until c);\n"
        "  a8: assert property (@(posedge clk) a |-> if (b) c else ##1 c);\n"
        "  a9: assert property (@(posedge clk) a[*2] ##1 b |-> s_eventually c);\n"
        "  a10: assert property (p1(a));\n"
        "  a11: assert property (@(posedge clk) not (a |-> b));\n"
        "  c1: cover property (@(posedge clk) a ##1 b);\n"
        "  a12: assert property (@(posedge clk) a |-> strong(##[1:$] b));\n"
        "  property p2(logic x, int unsigned y, bit [1:0] z, sequence sq, untyped u,\n"
        "              [3:0] w, signed v, local input int l, q, r = 1);\n"
        "    @(posedge clk) x |-> sq;\n"
        "  endproperty\n"
        "  sequence s2(x, logic y, [1:0] z, untyped u); @(posedge clk) x ##1 y; endsequence\n"
        "  let max(a, int b, [3:0] c, untyped d = 0) = (a > b) ? a : b;\n"
        "endmodule\n"
        "checker chk(x, logic y, event e, sequence s, property p, output bit o = 0);\n"
        "  a: assert property (@(posedge x) y |-> p);\n"
        "endchecker\n";
    CHECK(parseErrors(src) == 0);
    CHECK(parseErrors(src, true) == 0);
}
