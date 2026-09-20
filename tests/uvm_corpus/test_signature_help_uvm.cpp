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
// out-of-class `function ... uvm_component::name(...)` definition later) --
// both same-file, so pickBestSymbol's own same-file/earliest-line tie-break
// resolves to the in-class extern prototype's own Port data, not the
// out-of-class body's (whose own scope isn't nested under the class at all,
// a separate, already-disclosed gap -- see "Dot-completion into a
// package-nested class's members" in handoff.md). Either declaration
// carries the same parameter, so this doesn't change what's asserted below,
// only which of the two records SignatureHelpProvider happens to read it
// from.

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

TEST_CASE("signature help: still null for a dotted call inside an out-of-class method body, "
          "for a different, deeper reason than before plan.md §6.27",
          "[uvm_corpus][signature_help]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 2532 (1-based): "      m_children[c].set_domain(domain);", inside
    // `function void uvm_component::set_domain(...); ... endfunction` (an
    // out-of-class method body) -> 0-based line 2531, cursor right after
    // "set_domain(" at column 31.
    //
    // Before plan.md §6.27, this returned null simply because dotted calls
    // were categorically unsupported. Now that they're resolved via the
    // same chain-resolution machinery dot-completion uses
    // (dotCompletionContext/resolveChain, lsp/symbol_utils.h), this
    // *specific* real corpus site still returns null, but for a genuinely
    // different, already-disclosed reason confirmed by tracing it, not
    // assumed: an out-of-class method body's own recorded scope isn't
    // nested under its class at all (see this file's own header comment
    // above, and "Dot-completion into a package-nested class's members" in
    // handoff.md/plan.md §6.17 for the sibling gap this resembles) --
    // `m_children` (a `protected uvm_component m_children[string]` member)
    // is therefore never visible to `findSymbolsVisibleAt` from inside this
    // particular body, so the receiver chain fails to resolve before
    // `resolveMethod` is ever reached. A dotted call from *inside* an
    // ordinary in-class method body (this file's other two cases, and every
    // unit test in test_signature_help.cpp) is unaffected -- only an
    // out-of-class body's own member-visibility is the gap here.
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("base/uvm_component.svh", 2531, 31), uvmCorpusDb(), text);
    REQUIRE(result.isNull());
}
