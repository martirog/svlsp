#include <catch2/catch_test_macros.hpp>
#include "compiler/parse_record.h"
#include "compiler/sv_tree_walker.h"
#include <algorithm>
#include <fstream>
#include <sstream>

static WalkResult walkSource(const std::string& src) {
    return SvTreeWalker::walk(src);
}

static WalkResult walkFile(const std::string& path) {
    std::ifstream f(path);
    REQUIRE(f.is_open());
    std::ostringstream ss;
    ss << f.rdbuf();
    return SvTreeWalker::walk(ss.str());
}

static const ParseRecord* findRecord(const std::vector<ParseRecord>& recs,
                                      ParseRecordKind kind, const std::string& name = "") {
    for (const auto& r : recs)
        if (r.kind == kind && (name.empty() || r.name == name)) return &r;
    return nullptr;
}

static int countKind(const std::vector<ParseRecord>& recs, ParseRecordKind kind) {
    return static_cast<int>(
        std::count_if(recs.begin(), recs.end(),
                      [kind](const ParseRecord& r) { return r.kind == kind; }));
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

TEST_CASE("module record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource("module top; endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Module, "top");
    REQUIRE(r != nullptr);
    CHECK(r->line == 1);
}

TEST_CASE("module record from module_basic.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkFile(SV_EXAMPLES_DIR "/module_basic.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "adder") != nullptr);
}

TEST_CASE("multiple modules produce multiple records", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module a; endmodule\n"
        "module b; endmodule\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Module) == 2);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "a") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "b") != nullptr);
}

TEST_CASE("module line number is correct", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "\n"
        "\n"
        "module positioned; endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Module, "positioned");
    REQUIRE(r != nullptr);
    CHECK(r->line == 3);
}

// ---------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------

TEST_CASE("interface record from interfaces.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkFile(SV_EXAMPLES_DIR "/interfaces.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Interface, "bus_if") != nullptr);
}

TEST_CASE("interface record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource("interface my_if; endinterface\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Interface, "my_if") != nullptr);
}

// ---------------------------------------------------------------------------
// Package
// ---------------------------------------------------------------------------

TEST_CASE("package record from packages.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkFile(SV_EXAMPLES_DIR "/packages.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "math_pkg") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "bus_pkg") != nullptr);
}

TEST_CASE("package record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource("package my_pkg; endpackage\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "my_pkg") != nullptr);
}

// ---------------------------------------------------------------------------
// Class
// ---------------------------------------------------------------------------

TEST_CASE("class records from classes.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkFile(SV_EXAMPLES_DIR "/classes.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "Packet") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "ErrPacket") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "BurstPacket") != nullptr);
}

TEST_CASE("class record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource("class Foo; endclass\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "Foo") != nullptr);
}

// ---------------------------------------------------------------------------
// Function and Task
// ---------------------------------------------------------------------------

TEST_CASE("function and task records from functions_tasks.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkFile(SV_EXAMPLES_DIR "/functions_tasks.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "byte_reverse") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "clog2") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "drive_bus") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "wait_cycles") != nullptr);
}

TEST_CASE("function record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  function automatic int add(input int a, b); return a+b; endfunction\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "add") != nullptr);
}

TEST_CASE("task record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  task automatic delay(input int n); repeat(n) @(posedge clk); endtask\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "delay") != nullptr);
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------

TEST_CASE("ANSI port records emitted", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (input logic clk, input logic rst, output logic q);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Port) == 3);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "clk") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "rst") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "q") != nullptr);
}

TEST_CASE("port records from module_basic.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkFile(SV_EXAMPLES_DIR "/module_basic.sv");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Port) > 0);
}

// ---------------------------------------------------------------------------
// Parse errors
// ---------------------------------------------------------------------------

TEST_CASE("parse errors reported for malformed source", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource("module bad { endmodule\n");
    CHECK(!errs.empty());
}

TEST_CASE("zero parse errors for valid source", "[compiler][listener]") {
    auto [recs, errs, imps, insts, calls] = walkSource("module ok; endmodule\n");
    CHECK(errs.empty());
}

// ---------------------------------------------------------------------------
// Signal declarations — data_declaration
// ---------------------------------------------------------------------------

TEST_CASE("signal record from data_declaration inside module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  logic [7:0] data;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "data") != nullptr);
}

TEST_CASE("multiple signals from comma-separated data_declaration", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  logic a, b, c;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "a") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "b") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "c") != nullptr);
}

TEST_CASE("signal record from net_declaration inside module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  wire w;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "w") != nullptr);
}

TEST_CASE("signals from functions_tasks.sv captured", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkFile(SV_EXAMPLES_DIR "/functions_tasks.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "data") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "clk")  != nullptr);
}

// ---------------------------------------------------------------------------
// Signal detail (declared type, for dot-completion's object-type resolution
// — plan.md §6.10)
// ---------------------------------------------------------------------------

TEST_CASE("class-typed signal records its declared type in detail", "[compiler][listener][phase6.10]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  MyClass foo;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "foo");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "MyClass");
}

TEST_CASE("built-in-typed signal leaves detail empty", "[compiler][listener][phase6.10]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  logic [7:0] data;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "data");
    REQUIRE(r != nullptr);
    CHECK(r->detail.empty());
}

// ---------------------------------------------------------------------------
// Built-in container/type detail tags (plan.md §6.13 -- built-in method
// completion). Each tag/literal type name below is looked up by
// src/lsp/sv_builtin_methods.h's builtinMethodsFor() at the completion
// call site.
// ---------------------------------------------------------------------------

TEST_CASE("queue-typed signal is tagged $queue", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  int q[$];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_QUEUE);
}

TEST_CASE("associative-array-typed signal is tagged $assoc_array", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  int aa[string];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "aa");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_ASSOC);
}

TEST_CASE("dynamic-array-typed signal is tagged $dynamic_array", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  int arr[];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "arr");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_DYNAMIC_ARRAY);
}

TEST_CASE("fixed-size-array-typed signal is tagged $fixed_array", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  int arr[8];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "arr");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_FIXED_ARRAY);
}

TEST_CASE("string-typed signal is tagged $string", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  string s;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "s");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_STRING);
}

TEST_CASE("event-typed signal is tagged $event", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  event e;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "e");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_EVENT);
}

TEST_CASE("mailbox-typed signal records its declared type in detail (regression, no tree-walker change needed)",
          "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  mailbox #(int) mbx;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "mbx");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "mailbox");
}

TEST_CASE("process-typed signal records its declared type in detail (regression, no tree-walker change needed)",
          "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  process p;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "p");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "process");
}

TEST_CASE("semaphore-typed signal records its declared type in detail (regression, no tree-walker change needed)",
          "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  semaphore sem;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "sem");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "semaphore");
}

TEST_CASE("queue-typed and plain declarators sharing one data_type are tagged independently",
          "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  int a[$], b;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* ra = findRecord(recs, ParseRecordKind::Signal, "a");
    auto* rb = findRecord(recs, ParseRecordKind::Signal, "b");
    REQUIRE(ra != nullptr);
    REQUIRE(rb != nullptr);
    CHECK(ra->detail == CONTAINER_QUEUE);
    CHECK(rb->detail.empty());
}

// ---------------------------------------------------------------------------
// Layered container/element detail (plan.md §6.15 -- queue/associative-array
// element access completion). A container-typed declarator's detail now
// encodes the *whole* dimension shape, outermost first, ending in the
// element's own type/tag -- not just the outermost container's tag alone.
// ---------------------------------------------------------------------------

TEST_CASE("class-typed queue is tagged $queue:ClassName (element type appended)",
          "[compiler][listener][phase6.15]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class MyClass; endclass\n"
        "module m;\n"
        "  MyClass q[$];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == std::string(CONTAINER_QUEUE) + ":MyClass");
}

TEST_CASE("string-element queue is tagged $queue:$string (container wins, element still recorded)",
          "[compiler][listener][phase6.15]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  string s[$];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "s");
    REQUIRE(r != nullptr);
    CHECK(r->detail == std::string(CONTAINER_QUEUE) + ":" + CONTAINER_STRING);
}

TEST_CASE("int-element queue has no trailing element layer (unchanged from §6.13)",
          "[compiler][listener][phase6.15]") {
    // Regression: a built-in scalar element type has no tag, so the detail
    // stays exactly the single container tag, matching §6.13's original
    // (pre-§6.15) behavior for this case.
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  int q[$];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_QUEUE);
}

TEST_CASE("a fixed array of queues of a class records all three layers, outermost first",
          "[compiler][listener][phase6.15]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class MyClass; endclass\n"
        "module m;\n"
        "  MyClass arr[4][$];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "arr");
    REQUIRE(r != nullptr);
    CHECK(r->detail == std::string(CONTAINER_FIXED_ARRAY) + ":" + CONTAINER_QUEUE + ":MyClass");
}

TEST_CASE("a fixed array of queues of int has only the two dimension layers, no element layer",
          "[compiler][listener][phase6.15]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  int arr[4][$];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "arr");
    REQUIRE(r != nullptr);
    CHECK(r->detail == std::string(CONTAINER_FIXED_ARRAY) + ":" + CONTAINER_QUEUE);
}

TEST_CASE("associative array of a class is tagged $assoc_array:ClassName",
          "[compiler][listener][phase6.15]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class MyClass; endclass\n"
        "module m;\n"
        "  MyClass aa[string];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "aa");
    REQUIRE(r != nullptr);
    CHECK(r->detail == std::string(CONTAINER_ASSOC) + ":MyClass");
}

// ---------------------------------------------------------------------------
// Function/task prototypes -- pure virtual, extern, interface-class methods,
// DPI import (plan.md §6.16). None of these have a body (function_prototype/
// task_prototype, not function_body_declaration/task_body_declaration), so
// they need their own listeners -- previously entirely unrecorded.
// ---------------------------------------------------------------------------

TEST_CASE("pure virtual function is recorded as a Function symbol with its return type",
          "[compiler][listener][phase6.16]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "virtual class Base;\n"
        "  pure virtual function int get_val(int x);\n"
        "endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Function, "get_val");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "int");
    CHECK(r->endLine == 0);
}

TEST_CASE("pure virtual task is recorded as a Task symbol",
          "[compiler][listener][phase6.16]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "virtual class Base;\n"
        "  pure virtual task do_it(int x);\n"
        "endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Task, "do_it");
    REQUIRE(r != nullptr);
    CHECK(r->endLine == 0);
}

TEST_CASE("extern function/task prototypes are recorded even with no body anywhere in the source",
          "[compiler][listener][phase6.16]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class Base;\n"
        "  extern function int get_val(int x);\n"
        "  extern task do_it(int x);\n"
        "endclass\n");
    REQUIRE(errs.empty());
    auto* fn = findRecord(recs, ParseRecordKind::Function, "get_val");
    auto* tk = findRecord(recs, ParseRecordKind::Task, "do_it");
    REQUIRE(fn != nullptr);
    REQUIRE(tk != nullptr);
    CHECK(fn->detail == "int");
}

TEST_CASE("a pure-virtual method does not leak its scope onto declarations that follow it",
          "[compiler][listener][phase6.16]") {
    // Regression guard: pushId() unconditionally pushes a scope frame for
    // Function/Task records, normally popped by the body-form's own exit
    // listener. A prototype has no body -- if its own exit listener didn't
    // pop that frame, "y" below would be wrongly recorded as nested inside
    // get_val's scope instead of directly inside Base.
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class Base;\n"
        "  pure virtual function int get_val(int x);\n"
        "  int y;\n"
        "endclass\n");
    REQUIRE(errs.empty());
    auto* y = findRecord(recs, ParseRecordKind::Signal, "y");
    REQUIRE(y != nullptr);
    CHECK(y->parent == "Base");
    CHECK(y->scope == "Base");
}

TEST_CASE("interface class parses cleanly and records itself plus its pure-virtual methods",
          "[compiler][listener][phase6.16]") {
    // Regression test for a separate, related bug found while investigating
    // this: interface_class_declaration was defined in the grammar but never
    // referenced from any reachable parent rule, so this construct always
    // produced spurious parse errors before this fix.
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package p;\n"
        "  interface class Foo;\n"
        "    pure virtual function int get_val(int x);\n"
        "    pure virtual task do_it(int x);\n"
        "  endclass\n"
        "endpackage\n");
    REQUIRE(errs.empty());
    auto* cls = findRecord(recs, ParseRecordKind::Class, "Foo");
    REQUIRE(cls != nullptr);
    auto* fn = findRecord(recs, ParseRecordKind::Function, "get_val");
    auto* tk = findRecord(recs, ParseRecordKind::Task, "do_it");
    REQUIRE(fn != nullptr);
    REQUIRE(tk != nullptr);
    CHECK(fn->parent == "Foo");
    CHECK(tk->parent == "Foo");
}

TEST_CASE("interface class with multiple inheritance records only the first parent in detail",
          "[compiler][listener][phase6.16]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package p;\n"
        "  interface class A;\n"
        "    pure virtual function int a_fn();\n"
        "  endclass\n"
        "  interface class B;\n"
        "    pure virtual function int b_fn();\n"
        "  endclass\n"
        "  interface class C extends A, B;\n"
        "    pure virtual function int c_fn();\n"
        "  endclass\n"
        "endpackage\n");
    REQUIRE(errs.empty());
    auto* c = findRecord(recs, ParseRecordKind::Class, "C");
    REQUIRE(c != nullptr);
    CHECK(c->detail == "A");
}

TEST_CASE("a pure-virtual method still resolves cleanly when the class extends an unresolved external base",
          "[compiler][listener][phase6.16]") {
    // Models the real-world shape that motivated this fix: a class
    // extending an unresolved base (e.g. UVM's uvm_object, never declared
    // in this same file) with a pure-virtual method of its own.
    auto [recs, errs, imps, insts, calls] = walkSource(
        "virtual class Base extends uvm_object;\n"
        "  pure virtual function Base get_policy(int par);\n"
        "endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Function, "get_policy");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "Base");
}

TEST_CASE("a DPI-imported function is recorded as a Function symbol",
          "[compiler][listener][phase6.16]") {
    // Falls out for free: dpi_function_proto/dpi_task_proto reduce to the
    // same function_prototype/task_prototype rules pure-virtual/extern
    // methods use, so no separate handling is needed for DPI imports.
    auto [recs, errs, imps, insts, calls] = walkSource(
        "import \"DPI-C\" function int foo(int x);\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Function, "foo");
    REQUIRE(r != nullptr);
}

// ---------------------------------------------------------------------------
// Function/task parameters -- tf_port_item (plan.md §6.22 follow-up).
// Recorded as ParseRecordKind::Port (same shape a module port already is),
// scoped to the enclosing function/task's own scope chain, powering
// SignatureHelpProvider's extension to bare function/task calls.
// ---------------------------------------------------------------------------

TEST_CASE("a function parameter is recorded as a Port scoped to the function",
          "[compiler][listener][phase6.22]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "function int add(input int a);\n"
        "  return a;\n"
        "endfunction\n");
    REQUIRE(errs.empty());
    auto* p = findRecord(recs, ParseRecordKind::Port, "a");
    REQUIRE(p != nullptr);
    CHECK(p->parent == "add");
    CHECK(p->scope == "add");
    // Plain `input` is the default direction -- dropped from detail as
    // noise; only type remains.
    CHECK(p->detail == "int");
}

TEST_CASE("a task parameter is recorded as a Port scoped to the task",
          "[compiler][listener][phase6.22]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "task automatic do_thing(input int x);\n"
        "endtask\n");
    REQUIRE(errs.empty());
    auto* p = findRecord(recs, ParseRecordKind::Port, "x");
    REQUIRE(p != nullptr);
    CHECK(p->scope == "do_thing");
}

TEST_CASE("a non-default parameter direction is kept in detail",
          "[compiler][listener][phase6.22]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "function void foo(output int b);\n"
        "endfunction\n");
    REQUIRE(errs.empty());
    auto* p = findRecord(recs, ParseRecordKind::Port, "b");
    REQUIRE(p != nullptr);
    CHECK(p->detail == "output int");
}

TEST_CASE("a two-token parameter direction keeps its real spacing",
          "[compiler][listener][phase6.22]") {
    // Regression guard for the verbatim-token-stream extraction: ctx->getText()
    // would strip the space and yield "constrefint" instead.
    auto [recs, errs, imps, insts, calls] = walkSource(
        "function void foo(const ref int x);\n"
        "endfunction\n");
    REQUIRE(errs.empty());
    auto* p = findRecord(recs, ParseRecordKind::Port, "x");
    REQUIRE(p != nullptr);
    CHECK(p->detail == "const ref int");
}

TEST_CASE("a default parameter value is appended after the separator",
          "[compiler][listener][phase6.22]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "function void foo(int width = 8);\n"
        "endfunction\n");
    REQUIRE(errs.empty());
    auto* p = findRecord(recs, ParseRecordKind::Port, "width");
    REQUIRE(p != nullptr);
    CHECK(p->detail == std::string("int") + PARAM_DEFAULT_VALUE_SEP + " = 8");
}

TEST_CASE("a class method's parameters are scoped to Class::method",
          "[compiler][listener][phase6.22]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class C;\n"
        "  function int get(int i);\n"
        "    return i;\n"
        "  endfunction\n"
        "endclass\n");
    REQUIRE(errs.empty());
    auto* p = findRecord(recs, ParseRecordKind::Port, "i");
    REQUIRE(p != nullptr);
    CHECK(p->scope == "C::get");
}

TEST_CASE("a pure-virtual method's parameters are recorded even with no body",
          "[compiler][listener][phase6.22]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class C;\n"
        "  pure virtual function int compute(int a, int b);\n"
        "endclass\n");
    REQUIRE(errs.empty());
    auto* pa = findRecord(recs, ParseRecordKind::Port, "a");
    auto* pb = findRecord(recs, ParseRecordKind::Port, "b");
    REQUIRE(pa != nullptr);
    REQUIRE(pb != nullptr);
    CHECK(pa->scope == "C::compute");
    CHECK(pb->scope == "C::compute");
}

// ---------------------------------------------------------------------------
// Parent scope tracking
// ---------------------------------------------------------------------------

TEST_CASE("signal parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module my_mod;\n"
        "  logic sig;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "sig");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "my_mod");
}

TEST_CASE("port parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module top (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "clk");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "top");
}

TEST_CASE("function parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  function automatic int add(input int a, b); return a+b; endfunction\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Function, "add");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "m");
}

TEST_CASE("top-level module has empty parent", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource("module top_level; endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Module, "top_level");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "");
}

// ---------------------------------------------------------------------------
// Port direction in detail
// ---------------------------------------------------------------------------

TEST_CASE("input port detail is 'input <type>'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "clk");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "input logic");
}

TEST_CASE("output port detail is 'output <type>'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (output logic q);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "output logic");
}

TEST_CASE("inout port detail is 'inout <type>'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (inout wire bus);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "bus");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "inout wire");
}

// ---------------------------------------------------------------------------
// Module/interface/program port type information (plan.md §6.28).
// ---------------------------------------------------------------------------

TEST_CASE("a module port's declared type is recorded in detail",
          "[compiler][listener][phase6.28]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (input int width);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "width");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "input int");
}

TEST_CASE("a module port with a user-defined type is recorded in detail",
          "[compiler][listener][phase6.28]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (input MyIfcClass h);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "h");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "input MyIfcClass");
}

TEST_CASE("an untyped/implicit module port keeps only its direction",
          "[compiler][listener][phase6.28]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (input wire clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "clk");
    REQUIRE(r != nullptr);
    // `wire` alone is a net_type with an implicit data_type -- net_port_type
    // still captures the whole "wire" text, so this isn't truly untyped;
    // included to document that shape rather than assert a blank type.
    CHECK(r->detail == "input wire");
}

TEST_CASE("a module port's default value is appended after the separator",
          "[compiler][listener][phase6.28]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (input int width = 8);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "width");
    REQUIRE(r != nullptr);
    CHECK(r->detail == std::string("input int") + PARAM_DEFAULT_VALUE_SEP + " = 8");
}

TEST_CASE("an interface port's detail carries no separate type",
          "[compiler][listener][phase6.28]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (my_if.mp h);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "h");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "");
}

TEST_CASE("a variable-port-header (var) module port records its type",
          "[compiler][listener][phase6.28]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m (output var int q);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "output var int");
}

// ---------------------------------------------------------------------------
// Class hierarchy
// ---------------------------------------------------------------------------

TEST_CASE("class with extends has parent class in detail", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class Base; endclass\n"
        "class Child extends Base; endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Class, "Child");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "Base");
}

TEST_CASE("class without extends has empty detail", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource("class Standalone; endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Class, "Standalone");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "");
}

TEST_CASE("class hierarchy from classes.sv detail fields", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkFile(SV_EXAMPLES_DIR "/classes.sv");
    REQUIRE(errs.empty());
    auto* err_pkt = findRecord(recs, ParseRecordKind::Class, "ErrPacket");
    REQUIRE(err_pkt != nullptr);
    CHECK(err_pkt->detail == "Packet");
    auto* burst = findRecord(recs, ParseRecordKind::Class, "BurstPacket");
    REQUIRE(burst != nullptr);
    CHECK(burst->detail == "Packet");
}

// ---------------------------------------------------------------------------
// Function return type in detail
// ---------------------------------------------------------------------------

TEST_CASE("function detail contains return type text", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  function automatic int add(input int a, b); return a+b; endfunction\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Function, "add");
    REQUIRE(r != nullptr);
    CHECK(!r->detail.empty());
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

TEST_CASE("parameter record emitted for module parameter port", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m #(parameter int WIDTH = 8) (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Parameter, "WIDTH") != nullptr);
}

TEST_CASE("localparam record emitted inside module body", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  localparam int DEPTH = 16;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Parameter, "DEPTH") != nullptr);
}

TEST_CASE("parameter parent is the enclosing module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module param_mod #(parameter int N = 4) ();\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Parameter, "N");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "param_mod");
}

TEST_CASE("class-typed localparam records its declared type in detail", "[compiler][listener][phase6.10]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  localparam MyClass DEFAULT_FOO = null;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Parameter, "DEFAULT_FOO");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "MyClass");
}

TEST_CASE("built-in-typed parameter leaves detail empty", "[compiler][listener][phase6.10]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m #(parameter int N = 4) ();\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Parameter, "N");
    REQUIRE(r != nullptr);
    CHECK(r->detail.empty());
}

// ---------------------------------------------------------------------------
// Source map line translation
// ---------------------------------------------------------------------------

TEST_CASE("walk without source map leaves file field empty",
          "[compiler][listener][sourcemap]") {
    auto [recs, errs, imps, insts, calls] = walkSource("module m; endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Module, "m");
    REQUIRE(r != nullptr);
    CHECK(r->file == "");
    CHECK(r->line == 1);
}

TEST_CASE("walk with source map translates record line numbers",
          "[compiler][listener][sourcemap]") {
    // Compiled source has the module on line 3; source map says those lines
    // come from "real.sv" at lines 11, 12, 13.
    std::string src =
        "\n"
        "\n"
        "module translated; endmodule\n";

    std::vector<SourceLine> smap = {
        {"real.sv", 11},
        {"real.sv", 12},
        {"real.sv", 13},
    };

    auto result = SvTreeWalker::walk(src, smap);
    REQUIRE(result.parseErrors.empty());
    auto* r = findRecord(result.records, ParseRecordKind::Module, "translated");
    REQUIRE(r != nullptr);
    CHECK(r->file == "real.sv");
    CHECK(r->line == 13);
}

TEST_CASE("walk with source map translates error line numbers",
          "[compiler][listener][sourcemap]") {
    std::string src = "module bad { endmodule\n";

    std::vector<SourceLine> smap = {
        {"original.sv", 42},
    };

    auto result = SvTreeWalker::walk(src, smap);
    REQUIRE(!result.parseErrors.empty());
    CHECK(result.parseErrors[0].line == 42);
    CHECK(result.parseErrors[0].file == "original.sv");
}

TEST_CASE("walk with source map translates endLine on scope-defining records",
          "[compiler][listener][sourcemap]") {
    // Module spans compiled lines 1-3; source map says they are orig lines 10-12
    // in "src.sv".
    std::string src =
        "module scoped;\n"
        "  logic sig;\n"
        "endmodule\n";

    std::vector<SourceLine> smap = {
        {"src.sv", 10},
        {"src.sv", 11},
        {"src.sv", 12},
    };

    auto result = SvTreeWalker::walk(src, smap);
    REQUIRE(result.parseErrors.empty());
    auto* r = findRecord(result.records, ParseRecordKind::Module, "scoped");
    REQUIRE(r != nullptr);
    CHECK(r->line    == 10);
    CHECK(r->endLine == 12);
    CHECK(r->file    == "src.sv");
}

TEST_CASE("walk with source map translates column via colShifts",
          "[compiler][listener][sourcemap]") {
    // "sig" sits at compiled column 8 in "  logic sig;". A colShift says any
    // compiled column >= 8 on this line should have 5 added back, simulating
    // a mid-line macro invocation that shrank text earlier on the line.
    std::string src = "module m;\n  logic sig;\nendmodule\n";

    std::vector<SourceLine> smap = {
        {"", 1},
        {"", 2, {{8, 5}}},
        {"", 3},
    };

    auto result = SvTreeWalker::walk(src, smap);
    REQUIRE(result.parseErrors.empty());
    auto* r = findRecord(result.records, ParseRecordKind::Signal, "sig");
    REQUIRE(r != nullptr);
    CHECK(r->column == 13);
}

TEST_CASE("walk with source map leaves column unchanged when no colShifts present",
          "[compiler][listener][sourcemap]") {
    std::string src = "module m;\n  logic sig;\nendmodule\n";

    std::vector<SourceLine> smap = {
        {"", 1},
        {"", 2},
        {"", 3},
    };

    auto result = SvTreeWalker::walk(src, smap);
    REQUIRE(result.parseErrors.empty());
    auto* r = findRecord(result.records, ParseRecordKind::Signal, "sig");
    REQUIRE(r != nullptr);
    CHECK(r->column == 8);
}

// ---------------------------------------------------------------------------
// Package imports
// ---------------------------------------------------------------------------

TEST_CASE("specific import emits ImportRecord with item name",
          "[compiler][listener][import]") {
    auto result = walkSource("import util_pkg::MyClass;\nmodule m; endmodule\n");
    REQUIRE(result.imports.size() == 1);
    CHECK(result.imports[0].pkgName == "util_pkg");
    CHECK(result.imports[0].item   == "MyClass");
}

TEST_CASE("wildcard import emits ImportRecord with item *",
          "[compiler][listener][import]") {
    auto result = walkSource("import util_pkg::*;\nmodule m; endmodule\n");
    REQUIRE(result.imports.size() == 1);
    CHECK(result.imports[0].pkgName == "util_pkg");
    CHECK(result.imports[0].item   == "*");
}

TEST_CASE("multiple imports in one declaration emit one record each",
          "[compiler][listener][import]") {
    auto result = walkSource("import p1::A, p1::*;\nmodule m; endmodule\n");
    REQUIRE(result.imports.size() == 2);
    CHECK(result.imports[0].pkgName == "p1");
    CHECK(result.imports[0].item   == "A");
    CHECK(result.imports[1].pkgName == "p1");
    CHECK(result.imports[1].item   == "*");
}

TEST_CASE("imports from different packages are each recorded",
          "[compiler][listener][import]") {
    auto result = walkSource(
        "import pkg_a::*;\nimport pkg_b::Foo;\nmodule m; endmodule\n");
    REQUIRE(result.imports.size() == 2);
    auto it = std::find_if(result.imports.begin(), result.imports.end(),
                           [](const ImportRecord& r){ return r.pkgName == "pkg_a"; });
    REQUIRE(it != result.imports.end());
    CHECK(it->item == "*");
    auto it2 = std::find_if(result.imports.begin(), result.imports.end(),
                            [](const ImportRecord& r){ return r.pkgName == "pkg_b"; });
    REQUIRE(it2 != result.imports.end());
    CHECK(it2->item == "Foo");
}

TEST_CASE("file with no imports produces empty imports vector",
          "[compiler][listener][import]") {
    auto result = walkSource("module m; endmodule\n");
    CHECK(result.imports.empty());
}

// ---------------------------------------------------------------------------
// Package exports
// ---------------------------------------------------------------------------

TEST_CASE("plain import item is not marked isExport",
          "[compiler][listener][import][export]") {
    auto result = walkSource("import util_pkg::*;\nmodule m; endmodule\n");
    REQUIRE(result.imports.size() == 1);
    CHECK_FALSE(result.imports[0].isExport);
}

TEST_CASE("wildcard export emits ImportRecord marked isExport",
          "[compiler][listener][import][export]") {
    auto result = walkSource("package my_pkg; export util_pkg::*; endpackage\n");
    REQUIRE(result.imports.size() == 1);
    CHECK(result.imports[0].pkgName == "util_pkg");
    CHECK(result.imports[0].item   == "*");
    CHECK(result.imports[0].isExport);
}

TEST_CASE("specific export emits ImportRecord marked isExport",
          "[compiler][listener][import][export]") {
    auto result = walkSource("package my_pkg; export util_pkg::Foo; endpackage\n");
    REQUIRE(result.imports.size() == 1);
    CHECK(result.imports[0].pkgName == "util_pkg");
    CHECK(result.imports[0].item   == "Foo");
    CHECK(result.imports[0].isExport);
}

TEST_CASE("import inside a package that also exports is recorded distinctly",
          "[compiler][listener][import][export]") {
    auto result = walkSource(
        "package my_pkg; import base_pkg::*; export base_pkg::*; endpackage\n");
    REQUIRE(result.imports.size() == 2);
    CHECK(result.imports[0].pkgName == "base_pkg");
    CHECK_FALSE(result.imports[0].isExport);
    CHECK(result.imports[1].pkgName == "base_pkg");
    CHECK(result.imports[1].isExport);
}

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

TEST_CASE("program record emitted for inline source", "[compiler][listener][program]") {
    auto [recs, errs, imps, insts, calls] = walkSource("program my_prog; endprogram\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Program, "my_prog");
    REQUIRE(r != nullptr);
    CHECK(r->line == 1);
}

TEST_CASE("program endLine is backpatched", "[compiler][listener][program]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "program my_prog;\n"
        "  initial begin end\n"
        "endprogram\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Program, "my_prog");
    REQUIRE(r != nullptr);
    CHECK(r->endLine == 3);
}

// ---------------------------------------------------------------------------
// Module/interface/program instantiations
// ---------------------------------------------------------------------------

TEST_CASE("module instantiation emits InstantiationRecord",
          "[compiler][listener][instantiation]") {
    auto result = walkSource("module top; sub u0(); endmodule\n");
    REQUIRE(result.instantiations.size() == 1);
    CHECK(result.instantiations[0].typeName == "sub");
    CHECK(result.instantiations[0].instName == "u0");
    CHECK(result.instantiations[0].line == 1);
}

TEST_CASE("multiple comma-separated instances emit one record each",
          "[compiler][listener][instantiation]") {
    auto result = walkSource("module top; sub u0(), u1(); endmodule\n");
    REQUIRE(result.instantiations.size() == 2);
    CHECK(result.instantiations[0].typeName == "sub");
    CHECK(result.instantiations[0].instName == "u0");
    CHECK(result.instantiations[1].typeName == "sub");
    CHECK(result.instantiations[1].instName == "u1");
}

TEST_CASE("interface instantiation emits InstantiationRecord",
          "[compiler][listener][instantiation]") {
    auto result = walkSource(
        "interface bus_if; endinterface\n"
        "module top; bus_if u_bus(); endmodule\n");
    auto it = std::find_if(result.instantiations.begin(), result.instantiations.end(),
                           [](const InstantiationRecord& r){ return r.typeName == "bus_if"; });
    REQUIRE(it != result.instantiations.end());
    CHECK(it->instName == "u_bus");
}

TEST_CASE("program instantiation emits InstantiationRecord",
          "[compiler][listener][instantiation]") {
    auto result = walkSource(
        "program my_prog; endprogram\n"
        "module top; my_prog u_prog(); endmodule\n");
    auto it = std::find_if(result.instantiations.begin(), result.instantiations.end(),
                           [](const InstantiationRecord& r){ return r.typeName == "my_prog"; });
    REQUIRE(it != result.instantiations.end());
    CHECK(it->instName == "u_prog");
}

TEST_CASE("file with no instantiations produces empty instantiations vector",
          "[compiler][listener][instantiation]") {
    auto result = walkSource("module m; endmodule\n");
    CHECK(result.instantiations.empty());
}

// ---------------------------------------------------------------------------
// Bare function/task call sites (plan.md §6.23)
// ---------------------------------------------------------------------------

static const CallRecord* findCall(const std::vector<CallRecord>& calls, const std::string& name) {
    for (const auto& c : calls)
        if (c.calleeName == name) return &c;
    return nullptr;
}

TEST_CASE("bare call with two positional arguments emits a CallRecord with two "
          "positional slots", "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  function int f(int a, int b); f = a + b; endfunction\n"
        "  initial f(1, 2);\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "f");
    REQUIRE(c != nullptr);
    REQUIRE(c->args.size() == 2);
    CHECK(c->args[0].kind == CallArgSlot::Kind::Positional);
    CHECK(c->args[1].kind == CallArgSlot::Kind::Positional);
    CHECK(c->line == 3);
}

TEST_CASE("an elided positional slot is recorded between two supplied ones",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  function int f(int a, int b = 2, int c = 3); f = a+b+c; endfunction\n"
        "  initial f(1, , 5);\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "f");
    REQUIRE(c != nullptr);
    REQUIRE(c->args.size() == 3);
    CHECK(c->args[0].kind == CallArgSlot::Kind::Positional);
    CHECK(c->args[1].kind == CallArgSlot::Kind::Elided);
    CHECK(c->args[2].kind == CallArgSlot::Kind::Positional);
}

TEST_CASE("a trailing elided slot with nothing after the last comma is recorded",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  function int f(int a, int b = 2); f = a+b; endfunction\n"
        "  initial f(1, );\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "f");
    REQUIRE(c != nullptr);
    REQUIRE(c->args.size() == 2);
    CHECK(c->args[0].kind == CallArgSlot::Kind::Positional);
    CHECK(c->args[1].kind == CallArgSlot::Kind::Elided);
}

TEST_CASE("named argument connections are recorded with their port names",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  function int f(int a, int b); f = a + b; endfunction\n"
        "  initial f(.b(2), .a(1));\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "f");
    REQUIRE(c != nullptr);
    REQUIRE(c->args.size() == 2);
    CHECK(c->args[0].kind == CallArgSlot::Kind::Named);
    CHECK(c->args[0].name == "b");
    CHECK(c->args[1].kind == CallArgSlot::Kind::Named);
    CHECK(c->args[1].name == "a");
}

TEST_CASE("a positional prefix followed by a named tail is recorded in order",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  function int f(int a, int b, int c); f = a+b+c; endfunction\n"
        "  initial f(1, .c(3));\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "f");
    REQUIRE(c != nullptr);
    REQUIRE(c->args.size() == 2);
    CHECK(c->args[0].kind == CallArgSlot::Kind::Positional);
    CHECK(c->args[1].kind == CallArgSlot::Kind::Named);
    CHECK(c->args[1].name == "c");
}

TEST_CASE("a call with empty parens records zero argument slots",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  task t(); endtask\n"
        "  initial t();\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "t");
    REQUIRE(c != nullptr);
    CHECK(c->args.empty());
}

TEST_CASE("a task call with no parens at all records zero argument slots",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  task t(int a = 1); endtask\n"
        "  initial t;\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "t");
    REQUIRE(c != nullptr);
    CHECK(c->args.empty());
}

TEST_CASE("a dotted call is never recorded as a CallRecord",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  class Foo;\n"
        "    function int bar(int a); bar = a; endfunction\n"
        "  endclass\n"
        "  Foo f_inst;\n"
        "  initial f_inst.bar(1);\n"
        "endmodule\n");
    CHECK(findCall(result.calls, "bar") == nullptr);
}

TEST_CASE("a file with no calls produces an empty calls vector",
          "[compiler][listener][call]") {
    auto result = walkSource("module m; endmodule\n");
    CHECK(result.calls.empty());
}

// ---------------------------------------------------------------------------
// calleeScope extraction for Class::-qualified calls (plan.md §6.26)
// ---------------------------------------------------------------------------

TEST_CASE("an unqualified bare call records an empty calleeScope",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  function int f(int a); f = a; endfunction\n"
        "  initial f(1);\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "f");
    REQUIRE(c != nullptr);
    CHECK(c->calleeScope.empty());
}

TEST_CASE("a Class::-qualified call records the class name as calleeScope",
          "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  class Foo;\n"
        "    static function int bar(int a); bar = a; endfunction\n"
        "  endclass\n"
        "  initial Foo::bar(1);\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "bar");
    REQUIRE(c != nullptr);
    CHECK(c->calleeScope == "Foo");
}

TEST_CASE("a doubly-scoped call (T::type_id::create) records only the "
          "immediate scope", "[compiler][listener][call]") {
    auto result = walkSource(
        "module top;\n"
        "  class type_id;\n"
        "    static function int create(int a); create = a; endfunction\n"
        "  endclass\n"
        "  class T;\n"
        "  endclass\n"
        "  initial T::type_id::create(1);\n"
        "endmodule\n");
    auto* c = findCall(result.calls, "create");
    REQUIRE(c != nullptr);
    CHECK(c->calleeScope == "type_id");
}

// ---------------------------------------------------------------------------
// plan.md §6.30 step A: a `pkg::`/`Class::`-qualified type keeps its
// qualifier in `detail`, so resolution can tell pkg_a::Item from
// pkg_b::Item. Parameter value assignments (`#(...)`) are dropped -- only
// the name path matters for resolution.
// ---------------------------------------------------------------------------

TEST_CASE("package-qualified class-typed signal keeps the package in detail",
          "[compiler][listener][phase6.30]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package pkg_b; class Item; endclass endpackage\n"
        "module m;\n"
        "  pkg_b::Item x;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "x");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "pkg_b::Item");
}

TEST_CASE("package-qualified queue element keeps the package after the container tag",
          "[compiler][listener][phase6.30]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package pkg_b; class Item; endclass endpackage\n"
        "module m;\n"
        "  pkg_b::Item q[$];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == std::string(CONTAINER_QUEUE) + ":pkg_b::Item");
}

TEST_CASE("a nested class type keeps every qualifier; parameters are dropped",
          "[compiler][listener][phase6.30]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package p;\n"
        "  class Outer #(type T = int); class Inner; endclass endclass\n"
        "endpackage\n"
        "module m;\n"
        "  p::Outer#(int)::Inner n;\n"
        "  p::Outer#(int) o;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* n = findRecord(recs, ParseRecordKind::Signal, "n");
    REQUIRE(n != nullptr);
    CHECK(n->detail == "p::Outer::Inner");
    auto* o = findRecord(recs, ParseRecordKind::Signal, "o");
    REQUIRE(o != nullptr);
    CHECK(o->detail == "p::Outer");
}

TEST_CASE("a $unit-qualified type keeps the $unit qualifier",
          "[compiler][listener][phase6.30]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class Top; endclass\n"
        "module m;\n"
        "  $unit::Top t;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "t");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "$unit::Top");
}

TEST_CASE("a package-qualified extends keeps the package in the class's detail",
          "[compiler][listener][phase6.30]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package base_p; class Base; endclass endpackage\n"
        "class Child extends base_p::Base; endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Class, "Child");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "base_p::Base");
}

TEST_CASE("a parameterized extends records only the base class name",
          "[compiler][listener][phase6.30]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class Base #(type T = int); endclass\n"
        "class Child extends Base #(int); endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Class, "Child");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "Base");
}

// plan.md §6.30 step A, found building the resolver: data_declaration's
// first alternative accepts an implicit type, so a block's leading
// assignment statement `x = expr;` parses as a declaration of `x` (the LRM
// only allows an implicit-typed declaration with `var`). Those must not be
// recorded -- they'd shadow the real declaration in every scoped lookup.
TEST_CASE("a leading assignment statement in a function body is not recorded as a declaration",
          "[compiler][listener][phase6.30]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "class Base; int level; endclass\n"
        "class Derived extends Base;\n"
        "  int idx;\n"
        "  function void f(); level = 2; idx = level; endfunction\n"
        "endclass\n");
    REQUIRE(errs.empty());
    for (const auto& r : recs) {
        INFO(r.name << " at line " << r.line << " scope " << r.scope);
        CHECK_FALSE(r.scope == "Derived::f");
    }
}

TEST_CASE("implicit-typed declarations that really are declarations are still recorded",
          "[compiler][listener][phase6.30]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  initial begin\n"
        "    var v1 = 1;\n"
        "  end\n"
        "  function void f();\n"
        "    var [3:0] v2;\n"
        "    int v3;\n"
        "  endfunction\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    CHECK(findRecord(recs, ParseRecordKind::Signal, "v1") != nullptr);
    CHECK(findRecord(recs, ParseRecordKind::Signal, "v2") != nullptr);
    CHECK(findRecord(recs, ParseRecordKind::Signal, "v3") != nullptr);
}

// ---------------------------------------------------------------------------
// plan.md §6.30 step C: typedefs, enum literals, struct/union members and
// genvars are recorded as symbols (previously references/hover/definition
// found nothing for them).
// ---------------------------------------------------------------------------

TEST_CASE("a typedef is recorded as a Typedef, with its aliased type in detail",
          "[compiler][listener][phase6.30][typedef]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package p;\n"
        "  class Item; endclass\n"
        "  typedef logic [7:0] byte_t;\n"
        "  typedef Item item_alias_t;\n"
        "endpackage\n");
    REQUIRE(errs.empty());
    auto* b = findRecord(recs, ParseRecordKind::Typedef, "byte_t");
    REQUIRE(b != nullptr);
    CHECK(b->scope == "p");
    CHECK(b->line == 3);
    CHECK(b->column == 22);
    auto* a = findRecord(recs, ParseRecordKind::Typedef, "item_alias_t");
    REQUIRE(a != nullptr);
    CHECK(a->detail == "Item");
}

TEST_CASE("a forward typedef is not recorded (it must never outrank the real class)",
          "[compiler][listener][phase6.30][typedef]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "typedef class Fwd;\n"
        "class Fwd; endclass\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Typedef) == 0);
    CHECK(findRecord(recs, ParseRecordKind::Class, "Fwd") != nullptr);
}

TEST_CASE("enum literals are recorded in the enum's own scope",
          "[compiler][listener][phase6.30][enum]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  enum {IDLE, BUSY = 3} st;\n"
        "  typedef enum logic [1:0] {RED, GREEN} color_t;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::EnumLiteral) == 4);
    auto* idle = findRecord(recs, ParseRecordKind::EnumLiteral, "IDLE");
    REQUIRE(idle != nullptr);
    CHECK(idle->scope == "m");
    CHECK(idle->line == 2);
    CHECK(idle->column == 8);
    auto* green = findRecord(recs, ParseRecordKind::EnumLiteral, "GREEN");
    REQUIRE(green != nullptr);
    CHECK(green->scope == "m");
    CHECK(green->detail == "color_t");
    // The enum-typed variable and the typedef are still recorded.
    CHECK(findRecord(recs, ParseRecordKind::Signal, "st") != nullptr);
    CHECK(findRecord(recs, ParseRecordKind::Typedef, "color_t") != nullptr);
}

TEST_CASE("struct/union members are scoped under their variable or typedef",
          "[compiler][listener][phase6.30][struct]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  struct packed { logic [3:0] hi; logic [3:0] lo; } s;\n"
        "  typedef struct { int a; struct { int deep; } inner; } pair_t;\n"
        "  typedef union packed { logic [7:0] u8; logic [7:0] raw; } word_t;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* hi = findRecord(recs, ParseRecordKind::Member, "hi");
    REQUIRE(hi != nullptr);
    CHECK(hi->scope == "m::s");
    CHECK(hi->line == 2);
    CHECK(hi->column == 30);
    auto* a = findRecord(recs, ParseRecordKind::Member, "a");
    REQUIRE(a != nullptr);
    CHECK(a->scope == "m::pair_t");
    auto* inner = findRecord(recs, ParseRecordKind::Member, "inner");
    REQUIRE(inner != nullptr);
    CHECK(inner->scope == "m::pair_t");
    auto* deep = findRecord(recs, ParseRecordKind::Member, "deep");
    REQUIRE(deep != nullptr);
    CHECK(deep->scope == "m::pair_t::inner");
    auto* u8 = findRecord(recs, ParseRecordKind::Member, "u8");
    REQUIRE(u8 != nullptr);
    CHECK(u8->scope == "m::word_t");
    // Members are not Signals of the module.
    CHECK(findRecord(recs, ParseRecordKind::Signal, "hi") == nullptr);
    CHECK(findRecord(recs, ParseRecordKind::Signal, "s") != nullptr);
}

TEST_CASE("genvars are recorded from genvar declarations and inline loop genvars",
          "[compiler][listener][phase6.30][genvar]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "module m;\n"
        "  logic [3:0] v;\n"
        "  genvar g, h;\n"
        "  for (g = 0; g < 4; g++) begin : blk\n"
        "    assign v[g] = 1'b0;\n"
        "  end\n"
        "  for (genvar k = 0; k < 2; k++) begin : blk2 end\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Genvar) == 3);
    auto* g = findRecord(recs, ParseRecordKind::Genvar, "g");
    REQUIRE(g != nullptr);
    CHECK(g->scope == "m");
    CHECK(g->line == 3);
    CHECK(g->column == 9);
    auto* k = findRecord(recs, ParseRecordKind::Genvar, "k");
    REQUIRE(k != nullptr);
    CHECK(k->line == 7);
    CHECK(k->column == 14);
}

// ---------------------------------------------------------------------------
// plan.md §6.30 step D: an out-of-class method body (`function void
// C::m(); ... endfunction`) is recorded under its class, and its locals and
// arguments nest under `<class scope>::m`, so the class's members are
// visible inside it.
// ---------------------------------------------------------------------------

TEST_CASE("an out-of-class function body is scoped under its class, with its locals and "
          "arguments nested under it",
          "[compiler][listener][phase6.30][outofclass]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package p;\n"
        "  class C;\n"
        "    extern function void m(int a);\n"
        "  endclass\n"
        "  function void C::m(int a);\n"
        "    int t;\n"
        "  endfunction\n"
        "  int after;\n"
        "endpackage\n");
    REQUIRE(errs.empty());
    std::vector<const ParseRecord*> fns;
    for (const auto& r : recs)
        if (r.kind == ParseRecordKind::Function && r.name == "m") fns.push_back(&r);
    REQUIRE(fns.size() == 2); // the prototype and the body
    const ParseRecord* body = fns[0]->line == 5 ? fns[0] : fns[1];
    CHECK(body->line == 5);
    CHECK(body->scope == "p::C");
    CHECK(body->parent == "C");
    CHECK(body->endLine == 7);
    for (const auto& r : recs) {
        if (r.kind == ParseRecordKind::Signal && r.name == "t") CHECK(r.scope == "p::C::m");
        if (r.kind == ParseRecordKind::Port && r.name == "a" && r.line == 5)
            CHECK(r.scope == "p::C::m");
    }
    // The scope stack is restored after the body.
    auto* after = findRecord(recs, ParseRecordKind::Signal, "after");
    REQUIRE(after != nullptr);
    CHECK(after->scope == "p");
}

TEST_CASE("out-of-class task bodies, nested-class qualifiers and top-level classes",
          "[compiler][listener][phase6.30][outofclass]") {
    auto [recs, errs, imps, insts, calls] = walkSource(
        "package p;\n"
        "  class Outer;\n"
        "    class Inner; extern task run(); endclass\n"
        "  endclass\n"
        "  task Outer::Inner::run();\n"
        "    int local_r;\n"
        "  endtask\n"
        "endpackage\n"
        "class Top; extern function int get(); endclass\n"
        "function int Top::get();\n"
        "  int local_g;\n"
        "  return 0;\n"
        "endfunction\n");
    REQUIRE(errs.empty());
    for (const auto& r : recs) {
        if (r.kind == ParseRecordKind::Task && r.name == "run" && r.line == 5)
            CHECK(r.scope == "p::Outer::Inner");
        if (r.kind == ParseRecordKind::Function && r.name == "get" && r.line == 10)
            CHECK(r.scope == "Top");
    }
    auto* lr = findRecord(recs, ParseRecordKind::Signal, "local_r");
    REQUIRE(lr != nullptr);
    CHECK(lr->scope == "p::Outer::Inner::run");
    auto* lg = findRecord(recs, ParseRecordKind::Signal, "local_g");
    REQUIRE(lg != nullptr);
    CHECK(lg->scope == "Top::get");
}
