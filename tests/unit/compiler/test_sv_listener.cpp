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
    auto [recs, errs, imps] = walkSource("module top; endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Module, "top");
    REQUIRE(r != nullptr);
    CHECK(r->line == 1);
}

TEST_CASE("module record from module_basic.sv", "[compiler][listener]") {
    auto [recs, errs, imps] = walkFile(SV_EXAMPLES_DIR "/module_basic.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "adder") != nullptr);
}

TEST_CASE("multiple modules produce multiple records", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource(
        "module a; endmodule\n"
        "module b; endmodule\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Module) == 2);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "a") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "b") != nullptr);
}

TEST_CASE("module line number is correct", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource(
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
    auto [recs, errs, imps] = walkFile(SV_EXAMPLES_DIR "/interfaces.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Interface, "bus_if") != nullptr);
}

TEST_CASE("interface record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource("interface my_if; endinterface\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Interface, "my_if") != nullptr);
}

// ---------------------------------------------------------------------------
// Package
// ---------------------------------------------------------------------------

TEST_CASE("package record from packages.sv", "[compiler][listener]") {
    auto [recs, errs, imps] = walkFile(SV_EXAMPLES_DIR "/packages.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "math_pkg") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "bus_pkg") != nullptr);
}

TEST_CASE("package record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource("package my_pkg; endpackage\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "my_pkg") != nullptr);
}

// ---------------------------------------------------------------------------
// Class
// ---------------------------------------------------------------------------

TEST_CASE("class records from classes.sv", "[compiler][listener]") {
    auto [recs, errs, imps] = walkFile(SV_EXAMPLES_DIR "/classes.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "Packet") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "ErrPacket") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "BurstPacket") != nullptr);
}

TEST_CASE("class record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource("class Foo; endclass\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "Foo") != nullptr);
}

// ---------------------------------------------------------------------------
// Function and Task
// ---------------------------------------------------------------------------

TEST_CASE("function and task records from functions_tasks.sv", "[compiler][listener]") {
    auto [recs, errs, imps] = walkFile(SV_EXAMPLES_DIR "/functions_tasks.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "byte_reverse") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "clog2") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "drive_bus") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "wait_cycles") != nullptr);
}

TEST_CASE("function record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource(
        "module m;\n"
        "  function automatic int add(input int a, b); return a+b; endfunction\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "add") != nullptr);
}

TEST_CASE("task record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource(
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
    auto [recs, errs, imps] = walkSource(
        "module m (input logic clk, input logic rst, output logic q);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Port) == 3);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "clk") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "rst") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "q") != nullptr);
}

TEST_CASE("port records from module_basic.sv", "[compiler][listener]") {
    auto [recs, errs, imps] = walkFile(SV_EXAMPLES_DIR "/module_basic.sv");
    REQUIRE(errs.empty());
    CHECK(countKind(recs, ParseRecordKind::Port) > 0);
}

// ---------------------------------------------------------------------------
// Parse errors
// ---------------------------------------------------------------------------

TEST_CASE("parse errors reported for malformed source", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource("module bad { endmodule\n");
    CHECK(!errs.empty());
}

TEST_CASE("zero parse errors for valid source", "[compiler][listener]") {
    auto [recs, errs, imps] = walkSource("module ok; endmodule\n");
    CHECK(errs.empty());
}

// ---------------------------------------------------------------------------
// Signal declarations — data_declaration
// ---------------------------------------------------------------------------

TEST_CASE("signal record from data_declaration inside module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module m;\n"
        "  logic [7:0] data;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "data") != nullptr);
}

TEST_CASE("multiple signals from comma-separated data_declaration", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module m;\n"
        "  logic a, b, c;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "a") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "b") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "c") != nullptr);
}

TEST_CASE("signal record from net_declaration inside module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module m;\n"
        "  wire w;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "w") != nullptr);
}

TEST_CASE("signals from functions_tasks.sv captured", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkFile(SV_EXAMPLES_DIR "/functions_tasks.sv");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "data") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Signal, "clk")  != nullptr);
}

// ---------------------------------------------------------------------------
// Parent scope tracking
// ---------------------------------------------------------------------------

TEST_CASE("signal parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module my_mod;\n"
        "  logic sig;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Signal, "sig");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "my_mod");
}

TEST_CASE("port parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module top (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "clk");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "top");
}

TEST_CASE("function parent is the enclosing module name", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module m;\n"
        "  function automatic int add(input int a, b); return a+b; endfunction\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Function, "add");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "m");
}

TEST_CASE("top-level module has empty parent", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource("module top_level; endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Module, "top_level");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "");
}

// ---------------------------------------------------------------------------
// Port direction in detail
// ---------------------------------------------------------------------------

TEST_CASE("input port detail is 'input'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module m (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "clk");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "input");
}

TEST_CASE("output port detail is 'output'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module m (output logic q);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Port, "q");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "output");
}

TEST_CASE("inout port detail is 'inout'", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
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
    auto [recs, errs, imps] = walkSource(
        "class Base; endclass\n"
        "class Child extends Base; endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Class, "Child");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "Base");
}

TEST_CASE("class without extends has empty detail", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource("class Standalone; endclass\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Class, "Standalone");
    REQUIRE(r != nullptr);
    CHECK(r->detail == "");
}

TEST_CASE("class hierarchy from classes.sv detail fields", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkFile(SV_EXAMPLES_DIR "/classes.sv");
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
    auto [recs, errs, imps] = walkSource(
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
    auto [recs, errs, imps] = walkSource(
        "module m #(parameter int WIDTH = 8) (input logic clk);\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Parameter, "WIDTH") != nullptr);
}

TEST_CASE("localparam record emitted inside module body", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module m;\n"
        "  localparam int DEPTH = 16;\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    REQUIRE(findRecord(recs, ParseRecordKind::Parameter, "DEPTH") != nullptr);
}

TEST_CASE("parameter parent is the enclosing module", "[compiler][listener][phase44]") {
    auto [recs, errs, imps] = walkSource(
        "module param_mod #(parameter int N = 4) ();\n"
        "endmodule\n");
    REQUIRE(errs.empty());
    auto* r = findRecord(recs, ParseRecordKind::Parameter, "N");
    REQUIRE(r != nullptr);
    CHECK(r->parent == "param_mod");
}

// ---------------------------------------------------------------------------
// Source map line translation
// ---------------------------------------------------------------------------

TEST_CASE("walk without source map leaves file field empty",
          "[compiler][listener][sourcemap]") {
    auto [recs, errs, imps] = walkSource("module m; endmodule\n");
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
