#include <catch2/catch_test_macros.hpp>
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
    auto [recs, errs] = walkSource("module top; endmodule\n");
    REQUIRE(errs == 0);
    auto* r = findRecord(recs, ParseRecordKind::Module, "top");
    REQUIRE(r != nullptr);
    CHECK(r->line == 1);
}

TEST_CASE("module record from module_basic.sv", "[compiler][listener]") {
    auto [recs, errs] = walkFile(SV_EXAMPLES_DIR "/module_basic.sv");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "adder") != nullptr);
}

TEST_CASE("multiple modules produce multiple records", "[compiler][listener]") {
    auto [recs, errs] = walkSource(
        "module a; endmodule\n"
        "module b; endmodule\n");
    REQUIRE(errs == 0);
    CHECK(countKind(recs, ParseRecordKind::Module) == 2);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "a") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Module, "b") != nullptr);
}

TEST_CASE("module line number is correct", "[compiler][listener]") {
    auto [recs, errs] = walkSource(
        "\n"
        "\n"
        "module positioned; endmodule\n");
    REQUIRE(errs == 0);
    auto* r = findRecord(recs, ParseRecordKind::Module, "positioned");
    REQUIRE(r != nullptr);
    CHECK(r->line == 3);
}

// ---------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------

TEST_CASE("interface record from interfaces.sv", "[compiler][listener]") {
    auto [recs, errs] = walkFile(SV_EXAMPLES_DIR "/interfaces.sv");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Interface, "bus_if") != nullptr);
}

TEST_CASE("interface record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs] = walkSource("interface my_if; endinterface\n");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Interface, "my_if") != nullptr);
}

// ---------------------------------------------------------------------------
// Package
// ---------------------------------------------------------------------------

TEST_CASE("package record from packages.sv", "[compiler][listener]") {
    auto [recs, errs] = walkFile(SV_EXAMPLES_DIR "/packages.sv");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "math_pkg") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "bus_pkg") != nullptr);
}

TEST_CASE("package record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs] = walkSource("package my_pkg; endpackage\n");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Package, "my_pkg") != nullptr);
}

// ---------------------------------------------------------------------------
// Class
// ---------------------------------------------------------------------------

TEST_CASE("class records from classes.sv", "[compiler][listener]") {
    auto [recs, errs] = walkFile(SV_EXAMPLES_DIR "/classes.sv");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "Packet") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "ErrPacket") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "BurstPacket") != nullptr);
}

TEST_CASE("class record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs] = walkSource("class Foo; endclass\n");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Class, "Foo") != nullptr);
}

// ---------------------------------------------------------------------------
// Function and Task
// ---------------------------------------------------------------------------

TEST_CASE("function and task records from functions_tasks.sv", "[compiler][listener]") {
    auto [recs, errs] = walkFile(SV_EXAMPLES_DIR "/functions_tasks.sv");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "byte_reverse") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "clog2") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "drive_bus") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "wait_cycles") != nullptr);
}

TEST_CASE("function record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs] = walkSource(
        "module m;\n"
        "  function automatic int add(input int a, b); return a+b; endfunction\n"
        "endmodule\n");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Function, "add") != nullptr);
}

TEST_CASE("task record emitted for inline source", "[compiler][listener]") {
    auto [recs, errs] = walkSource(
        "module m;\n"
        "  task automatic delay(input int n); repeat(n) @(posedge clk); endtask\n"
        "endmodule\n");
    REQUIRE(errs == 0);
    REQUIRE(findRecord(recs, ParseRecordKind::Task, "delay") != nullptr);
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------

TEST_CASE("ANSI port records emitted", "[compiler][listener]") {
    auto [recs, errs] = walkSource(
        "module m (input logic clk, input logic rst, output logic q);\n"
        "endmodule\n");
    REQUIRE(errs == 0);
    CHECK(countKind(recs, ParseRecordKind::Port) == 3);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "clk") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "rst") != nullptr);
    REQUIRE(findRecord(recs, ParseRecordKind::Port, "q") != nullptr);
}

TEST_CASE("port records from module_basic.sv", "[compiler][listener]") {
    auto [recs, errs] = walkFile(SV_EXAMPLES_DIR "/module_basic.sv");
    REQUIRE(errs == 0);
    CHECK(countKind(recs, ParseRecordKind::Port) > 0);
}

// ---------------------------------------------------------------------------
// Parse errors
// ---------------------------------------------------------------------------

TEST_CASE("parse errors reported for malformed source", "[compiler][listener]") {
    auto [recs, errs] = walkSource("module bad { endmodule\n");
    CHECK(errs > 0);
}

TEST_CASE("zero parse errors for valid source", "[compiler][listener]") {
    auto [recs, errs] = walkSource("module ok; endmodule\n");
    CHECK(errs == 0);
}
