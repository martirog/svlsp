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
