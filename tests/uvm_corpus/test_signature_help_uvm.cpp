#include <catch2/catch_test_macros.hpp>
#include "lsp/signature_help.h"
#include "uvm_corpus_fixture.h"

// textDocument/signatureHelp against the real, full UVM corpus DB --
// specifically the plan.md §6.22 follow-up (bare function/task calls),
// since the UVM library corpus has no modules/interfaces to instantiate at
// all (it's a pure class library) -- the module/interface/program-port-list
// half of SignatureHelpProvider has no real corpus fixture to exercise here.
//
// All three call sites live in `uvm_component::do_execute_op`
// (base/uvm_component.svh), a real out-of-class method body:
//   do begin
//     child_comp = get_child(name);
//     ...
//   end while (get_next_child(name));
// `get_child`/`get_next_child` are each declared twice in this file (an
// `extern function ...;` prototype inside the class body, plus the real
// out-of-class `function ... uvm_component::name(...)` definition later).
// Since plan.md §6.30 step D both sit in scope uvm_pkg::uvm_component and
// their parameters share one scope; SymbolDatabase::portsOf keeps only the
// first declaration's (the prototype's), so each parameter is listed once.

namespace {

lsp::SignatureHelpParams makeParams(const std::string& relPath, unsigned line, unsigned col) {
    lsp::SignatureHelpParams p;
    p.textDocument.uri   = uriForRelPath(relPath);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

} // namespace

TEST_CASE("signature help: bare call to a single-parameter method resolves to its real parameter",
          "[uvm_corpus][signature_help]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 3505 (1-based): "          child_comp = get_child(name);"
    // -> 0-based line 3504, cursor right after "get_child(" at column 33.
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("base/uvm_component.svh", 3504, 33), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());

    const auto& sig = result.value().signatures[0];
    REQUIRE(sig.parameters.has_value());
    CHECK(sig.label == "get_child(string name)");
    CHECK(sig.parameters->size() == 1);
    REQUIRE(sig.activeParameter.has_value());
    CHECK(*sig.activeParameter == 0);
}

TEST_CASE("signature help: bare call to a ref-direction parameter keeps the direction in the label",
          "[uvm_corpus][signature_help]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 3511 (1-based): "        end while (get_next_child(name));"
    // -> 0-based line 3510, cursor right after "get_next_child(" at column 34.
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("base/uvm_component.svh", 3510, 34), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());

    const auto& sig = result.value().signatures[0];
    CHECK(sig.label == "get_next_child(ref string name)");
}

TEST_CASE("signature help: a dotted call through a class field inside an out-of-class method "
          "body resolves (plan.md §6.30 step D)",
          "[uvm_corpus][signature_help]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 2532 (1-based): "      m_children[c].set_domain(domain);", inside
    // `function void uvm_component::set_domain(...); ... endfunction` (an
    // out-of-class method body) -> 0-based line 2531, cursor right after
    // "set_domain(" at column 31.
    //
    // This returned null until §6.30 step D: the body's scope wasn't nested
    // under uvm_component, so `m_children` (a `protected uvm_component
    // m_children[string]` field) wasn't visible and the receiver chain
    // failed. The prototype (line 618) carries `hier=1`.
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("base/uvm_component.svh", 2531, 31), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());
    const auto& sig = result.value().signatures[0];
    REQUIRE(sig.parameters.has_value());
    CHECK(sig.parameters->size() == 2);
    CHECK(sig.label == "set_domain(uvm_domain domain, int hier = 1)");
    REQUIRE(sig.activeParameter.has_value());
    CHECK(*sig.activeParameter == 0);
}

TEST_CASE("signature help: UVM message macros resolve through uvm_macros.svh's includes "
          "(plan.md §6.29 part A)",
          "[uvm_corpus][signature_help]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");

    // Line 1935 (1-based): `    `uvm_error("INVSTNM", $sformatf("...", name))`
    // -> cursor right after `"INVSTNM", ` (column 26) is on MSG.
    auto err = SignatureHelpProvider::getSignatureHelp(
        makeParams("base/uvm_component.svh", 1934, 26), uvmCorpusDb(), text);
    REQUIRE_FALSE(err.isNull());
    const auto& esig = err.value().signatures[0];
    CHECK(esig.label == "`uvm_error(ID, MSG)");
    REQUIRE(esig.activeParameter.has_value());
    CHECK(*esig.activeParameter == 1);

    // Same line, inside `$sformatf(` (column 36): the system function's help.
    auto fmt = SignatureHelpProvider::getSignatureHelp(
        makeParams("base/uvm_component.svh", 1934, 36), uvmCorpusDb(), text);
    REQUIRE_FALSE(fmt.isNull());
    CHECK(fmt.value().signatures[0].label == "$sformatf(format, args...)");

    // Line 1745: `    `uvm_info("NEWCOMP", {"Creating ",` -- a multi-line
    // invocation; cursor right after `uvm_info(` (column 14) is on ID.
    auto info = SignatureHelpProvider::getSignatureHelp(
        makeParams("base/uvm_component.svh", 1744, 14), uvmCorpusDb(), text);
    REQUIRE_FALSE(info.isNull());
    const auto& isig = info.value().signatures[0];
    CHECK(isig.label == "`uvm_info(ID, MSG, VERBOSITY)");
    REQUIRE(isig.activeParameter.has_value());
    CHECK(*isig.activeParameter == 0);
}
