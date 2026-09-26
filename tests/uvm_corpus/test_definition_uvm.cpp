#include <catch2/catch_test_macros.hpp>
#include "lsp/definition.h"
#include "uvm_corpus_fixture.h"

// textDocument/definition against the real, full UVM corpus DB.
//
// Centerpiece: the exact symbol-table-pollution regression session 3 found
// and the token-pasting fix (session 4) resolved. `base/uvm_component.svh:59`
// declares `virtual class uvm_component extends uvm_report_object;` --
// before the token-pasting fix, a broken `uvm_register_cb` expansion at
// base/uvm_report_catcher.svh:71 created a bogus `Signal` symbol literally
// named `uvm_report_object`, which silently shadowed the real `Class` at
// base/uvm_report_object.svh:98 in hover/definition (no kind preference,
// alphabetical file ordering happened to put report_catcher.svh first).
// Definition on `uvm_report_object` here must now resolve to the real class.

namespace {

lsp::DefinitionParams makeParams(const std::string& relPath, unsigned line, unsigned col) {
    lsp::DefinitionParams p;
    p.textDocument.uri   = uriForRelPath(relPath);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

} // namespace

TEST_CASE("definition: uvm_report_object base-class reference resolves to the real class, not the former bogus symbol",
          "[uvm_corpus][definition]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 59 (1-based) -> 0-based 58: "virtual class uvm_component extends uvm_report_object;"
    // "uvm_report_object" starts at column 36.
    auto result = DefinitionProvider::getDefinition(makeParams("base/uvm_component.svh", 58, 36), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());
    REQUIRE(result.holdsAlternative<lsp::Definition>());
    const auto& def = result.get<lsp::Definition>();
    REQUIRE(std::holds_alternative<lsp::Location>(def));
    const auto& loc = std::get<lsp::Location>(def);

    CHECK(std::string(loc.uri.path()) == expectedUriPath("base/uvm_report_object.svh"));
    // Line 98 (1-based) -> 0-based 97.
    CHECK(loc.range.start.line == 97u);
}

TEST_CASE("definition: uvm_transaction base-class reference resolves correctly (positive control)",
          "[uvm_corpus][definition]") {
    const std::string text = readCorpusFile("seq/uvm_sequence_item.svh");
    // Line 52 (1-based) -> 0-based 51: "class uvm_sequence_item extends uvm_transaction;"
    // "uvm_transaction" starts at column 32.
    auto result = DefinitionProvider::getDefinition(makeParams("seq/uvm_sequence_item.svh", 51, 32), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());
    const auto& loc = std::get<lsp::Location>(result.get<lsp::Definition>());

    CHECK(std::string(loc.uri.path()) == expectedUriPath("base/uvm_transaction.svh"));
    // Line 139 (1-based) -> 0-based 138.
    CHECK(loc.range.start.line == 138u);
}

TEST_CASE("definition: inside an out-of-class method body, a class field and a bare method call "
          "land on uvm_component's own declarations (plan.md §6.30 step D)",
          "[uvm_corpus][definition]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");

    // Line 2532 (1-based) "      m_children[c].set_domain(domain);" inside
    // `function void uvm_component::set_domain(...)` (out-of-class body) ->
    // the field at line 1585 "  protected     uvm_component m_children[string];"
    // (col 30), not uvm_printer's same-named field.
    auto field = DefinitionProvider::getDefinition(makeParams("base/uvm_component.svh", 2531, 6),
                                                   uvmCorpusDb(), text);
    REQUIRE_FALSE(field.isNull());
    const auto& floc = std::get<lsp::Location>(field.get<lsp::Definition>());
    CHECK(std::string(floc.uri.path()) == expectedUriPath("base/uvm_component.svh"));
    CHECK(floc.range.start.line == 1584u);
    CHECK(floc.range.start.character == 30u);

    // Line 3505 (1-based) "          child_comp = get_child(name);" inside
    // uvm_component::do_execute_op's out-of-class body -> the extern
    // prototype at line 122 (col 32), not the out-of-class definition.
    auto call = DefinitionProvider::getDefinition(makeParams("base/uvm_component.svh", 3504, 23),
                                                  uvmCorpusDb(), text);
    REQUIRE_FALSE(call.isNull());
    const auto& cloc = std::get<lsp::Location>(call.get<lsp::Definition>());
    CHECK(std::string(cloc.uri.path()) == expectedUriPath("base/uvm_component.svh"));
    CHECK(cloc.range.start.line == 121u);
    CHECK(cloc.range.start.character == 32u);
}
