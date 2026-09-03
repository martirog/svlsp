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
    auto [recs, errs, imps, insts] = walkSource("module top; endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Module, "top");
    REQUIRE(r != nullptr);
    CHECK(r->line == 1);
}

TEST_CASE("module record from module_basic.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkFile(SV_EXAMPLES_DIR "/module_basic.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "adder") != nullptr);
}

TEST_CASE("multiple modules produce multiple records", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module a; endmodule\n"
        "module b; endmodule\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Module) == 2);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "a") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "b") != nullptr);
}

TEST_CASE("module line number is correct", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkFile(SV_EXAMPLES_DIR "/interfaces.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Interface, "bus_if") != nullptr);
}

TEST_CASE("interface record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource("interface my_if; endinterface\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Interface, "my_if") != nullptr);
}

// ---------------------------------------------------------------------------
// Package
// ---------------------------------------------------------------------------

TEST_CASE("package record from packages.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkFile(SV_EXAMPLES_DIR "/packages.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "math_pkg") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "bus_pkg") != nullptr);
}

TEST_CASE("package record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource("package my_pkg; endpackage\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "my_pkg") != nullptr);
}

// ---------------------------------------------------------------------------
// Class
// ---------------------------------------------------------------------------

TEST_CASE("class records from classes.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkFile(SV_EXAMPLES_DIR "/classes.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "Packet") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "ErrPacket") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "BurstPacket") != nullptr);
}

TEST_CASE("class record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource("class Foo; endclass\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "Foo") != nullptr);
}

// ---------------------------------------------------------------------------
// Function and Task
// ---------------------------------------------------------------------------

TEST_CASE("function and task records from functions_tasks.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkFile(SV_EXAMPLES_DIR "/functions_tasks.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "byte_reverse") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "clog2") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "drive_bus") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "wait_cycles") != nullptr);
}

TEST_CASE("function record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  function automatic int add(input int a, b); return a+b; endfunction\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "add") != nullptr);
}

TEST_CASE("task record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
        "module m (input logic clk, input logic rst, output logic q);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Port) == 3);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "clk") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "rst") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "q") != nullptr);
}

TEST_CASE("port records from module_basic.sv", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkFile(SV_EXAMPLES_DIR "/module_basic.sv");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Port) > 0);
}

// ---------------------------------------------------------------------------
// Parse errors
// ---------------------------------------------------------------------------

TEST_CASE("parse errors reported for malformed source", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource("module bad { endmodule\n");
    CHECK(!errs.empty());
}

TEST_CASE("zero parse errors for valid source", "[compiler][listener]") {
    auto [recs, errs, imps, insts] = walkSource("module ok; endmodule\n");
    CHECK(errs.empty());
}

// ---------------------------------------------------------------------------
// Signal declarations — data_declaration
// ---------------------------------------------------------------------------

TEST_CASE("signal record from data_declaration inside module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  logic [7:0] data;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "data") != nullptr);
}

TEST_CASE("multiple signals from comma-separated data_declaration", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  logic a, b, c;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "a") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "b") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "c") != nullptr);
}

TEST_CASE("signal record from net_declaration inside module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  wire w;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "w") != nullptr);
}

TEST_CASE("signals from functions_tasks.sv captured", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkFile(SV_EXAMPLES_DIR "/functions_tasks.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "data") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "clk")  != nullptr);
}

// ---------------------------------------------------------------------------
// Signal detail (declared type, for dot-completion's object-type resolution
// — plan.md §6.10)
// ---------------------------------------------------------------------------

TEST_CASE("class-typed signal records its declared type in detail", "[compiler][listener][phase6.10]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  MyClass foo;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "foo");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "MyClass");
}

TEST_CASE("built-in-typed signal leaves detail empty", "[compiler][listener][phase6.10]") {
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  int q[$];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_QUEUE);
}

TEST_CASE("associative-array-typed signal is tagged $assoc_array", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  int aa[string];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "aa");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_ASSOC);
}

TEST_CASE("dynamic-array-typed signal is tagged $dynamic_array", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  int arr[];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "arr");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_DYNAMIC_ARRAY);
}

TEST_CASE("fixed-size-array-typed signal is tagged $fixed_array", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  int arr[8];\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "arr");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_FIXED_ARRAY);
}

TEST_CASE("string-typed signal is tagged $string", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  string s;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "s");
    REQUIRE(r != nullptr);
    CHECK(r->detail == CONTAINER_STRING);
}

TEST_CASE("event-typed signal is tagged $event", "[compiler][listener][phase6.13]") {
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
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
// Parent scope tracking
// ---------------------------------------------------------------------------

TEST_CASE("signal parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module my_mod;\n"
        "  logic sig;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "sig");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "my_mod");
}

TEST_CASE("port parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module top (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "clk");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "top");
}

TEST_CASE("function parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  function automatic int add(input int a, b); return a+b; endfunction\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Function, "add");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "m");
}

TEST_CASE("top-level module has empty parent", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource("module top_level; endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Module, "top_level");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "");
}

// ---------------------------------------------------------------------------
// Port direction in detail
// ---------------------------------------------------------------------------

TEST_CASE("input port detail is 'input'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "clk");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "input");
}

TEST_CASE("output port detail is 'output'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m (output logic q);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "output");
}

TEST_CASE("inout port detail is 'inout'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m (inout wire bus);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "bus");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "inout");
}

// ---------------------------------------------------------------------------
// Class hierarchy
// ---------------------------------------------------------------------------

TEST_CASE("class with extends has parent class in detail", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "class Base; endclass\n"
        "class Child extends Base; endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Class, "Child");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "Base");
}

TEST_CASE("class without extends has empty detail", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource("class Standalone; endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Class, "Standalone");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "");
}

TEST_CASE("class hierarchy from classes.sv detail fields", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkFile(SV_EXAMPLES_DIR "/classes.sv");
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
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource(
        "module m #(parameter int WIDTH = 8) (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Parameter, "WIDTH") != nullptr);
}

TEST_CASE("localparam record emitted inside module body", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  localparam int DEPTH = 16;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Parameter, "DEPTH") != nullptr);
}

TEST_CASE("parameter parent is the enclosing module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module param_mod #(parameter int N = 4) ();\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Parameter, "N");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "param_mod");
}

TEST_CASE("class-typed localparam records its declared type in detail", "[compiler][listener][phase6.10]") {
    auto [recs, errs, imps, insts] = walkSource(
        "module m;\n"
        "  localparam MyClass DEFAULT_FOO = null;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Parameter, "DEFAULT_FOO");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "MyClass");
}

TEST_CASE("built-in-typed parameter leaves detail empty", "[compiler][listener][phase6.10]") {
    auto [recs, errs, imps, insts] = walkSource(
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
    auto [recs, errs, imps, insts] = walkSource("module m; endmodule\n");
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
    auto [recs, errs, imps, insts] = walkSource("program my_prog; endprogram\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Program, "my_prog");
    REQUIRE(r != nullptr);
    CHECK(r->line == 1);
}

TEST_CASE("program endLine is backpatched", "[compiler][listener][program]") {
    auto [recs, errs, imps, insts] = walkSource(
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
