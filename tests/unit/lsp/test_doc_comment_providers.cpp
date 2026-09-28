#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"
#include "lsp/hover.h"
#include "lsp/signature_help.h"
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/symbol_database.h"

// Doc comments shown by hover, signature help and completion resolve
// (plan.md §6.31). Each case compiles real source, so the walker's
// extraction and the DB round trip are exercised end to end.

namespace {

struct Fixture {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    CompilationController cc{sdb};
    Fixture() { db.initSchema(); }
};

const std::string kPath = "/docs/doc_prov.sv";

const std::string kSrc =
    "// Max width.\n"                         // 0
    "`define DOCP_W 32\n"                     // 1
    "// Logs a message.\n"                    // 2
    "`define DOCP_LOG(msg) $display(msg)\n"   // 3
    "// A documented class.\n"                // 4
    "// Second line.\n"                       // 5
    "class docp_c;\n"                         // 6
    "  // How many.\n"                        // 7
    "  int docp_count;\n"                     // 8
    "  // Adds two numbers.\n"                // 9
    "  function int docp_add(\n"              // 10
    "    int a, // The first.\n"              // 11
    "    int b);\n"                           // 12
    "    return a + b;\n"                     // 13
    "  endfunction\n"                         // 14
    "  extern function void docp_ext();\n"    // 15
    "  function void run();\n"                // 16
    "    docp_add(1, 2);\n"                   // 17
    "    `DOCP_LOG(\"x\");\n"                 // 18
    "    docp_\n"                             // 19
    "  endfunction\n"                         // 20
    "endclass\n"                              // 21
    "// Documented on the body.\n"            // 22
    "function void docp_c::docp_ext();\n"     // 23
    "endfunction\n";                          // 24

template <class P>
P at(unsigned line, unsigned col) {
    P p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(kPath);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

std::string hoverText(Fixture& f, unsigned line, unsigned col) {
    auto h = HoverProvider::getHover(at<lsp::HoverParams>(line, col), f.sdb, kSrc);
    REQUIRE_FALSE(h.isNull());
    return std::string(std::get<lsp::MarkupContent>(h->contents).value);
}

std::string docString(const lsp::Opt<lsp::OneOf<lsp::String, lsp::MarkupContent>>& doc) {
    if (!doc) return "";
    if (auto* s = std::get_if<lsp::String>(&*doc)) return *s;
    return std::get<lsp::MarkupContent>(*doc).value;
}

} // namespace

TEST_CASE("hover shows a declaration's doc under the header", "[lsp][hover][phase6.31]") {
    Fixture f;
    f.cc.compile(kPath, kSrc);
    const std::string cls = hoverText(f, 6, 7);
    CHECK(cls.find("**Class** `docp_c`") == 0);
    // Line breaks kept as markdown hard breaks.
    CHECK(cls.find("A documented class.  \nSecond line.") != std::string::npos);
    CHECK(hoverText(f, 8, 8).find("How many.") != std::string::npos);
    CHECK(hoverText(f, 11, 8).find("The first.") != std::string::npos);
}

TEST_CASE("hover on an undocumented symbol is unchanged", "[lsp][hover][phase6.31]") {
    Fixture f;
    f.cc.compile(kPath, kSrc);
    CHECK(hoverText(f, 16, 17) == "**Function** `run` → `void`\n\nin *docp_c*");
}

TEST_CASE("hover shows a macro's doc", "[lsp][hover][phase6.31]") {
    Fixture f;
    f.cc.compile(kPath, kSrc);
    CHECK(hoverText(f, 18, 6).find("Logs a message.") != std::string::npos);
}

TEST_CASE("hover on an extern prototype falls back to the out-of-class body's doc",
          "[lsp][hover][phase6.31]") {
    Fixture f;
    f.cc.compile(kPath, kSrc);
    CHECK(hoverText(f, 15, 24).find("Documented on the body.") != std::string::npos);
}

TEST_CASE("hover shows no doc when collection is off", "[lsp][hover][phase6.31]") {
    Fixture f;
    f.cc.setCollectDocs(false);
    f.cc.compile(kPath, kSrc);
    CHECK(hoverText(f, 6, 7).find("documented") == std::string::npos);
}

TEST_CASE("signature help carries the function's and its parameters' docs",
          "[lsp][signature-help][phase6.31]") {
    Fixture f;
    f.cc.compile(kPath, kSrc);
    auto help = SignatureHelpProvider::getSignatureHelp(
        at<lsp::SignatureHelpParams>(17, 13), f.sdb, kSrc);
    REQUIRE_FALSE(help.isNull());
    const auto& sig = help->signatures.at(0);
    CHECK(docString(sig.documentation) == "Adds two numbers.");
    REQUIRE(sig.parameters.has_value());
    REQUIRE(sig.parameters->size() == 2);
    CHECK(docString((*sig.parameters)[0].documentation) == "The first.");
    CHECK_FALSE((*sig.parameters)[1].documentation.has_value());
}

TEST_CASE("signature help carries a macro's doc", "[lsp][signature-help][phase6.31]") {
    Fixture f;
    f.cc.compile(kPath, kSrc);
    auto help = SignatureHelpProvider::getSignatureHelp(
        at<lsp::SignatureHelpParams>(18, 14), f.sdb, kSrc);
    REQUIRE_FALSE(help.isNull());
    CHECK(docString(help->signatures.at(0).documentation) == "Logs a message.");
}

TEST_CASE("completion items carry data, and resolve fills in the doc",
          "[lsp][completion][phase6.31]") {
    Fixture f;
    f.cc.compile(kPath, kSrc);
    auto result = CompletionProvider::getCompletion(
        at<lsp::CompletionParams>(19, 9), f.sdb, kSrc);
    REQUIRE_FALSE(result.isNull());
    auto* items = &result.get<lsp::Array<lsp::CompletionItem>>();
    auto find = [&](const std::string& label) -> const lsp::CompletionItem* {
        for (const auto& it : *items)
            if (it.label == label) return &it;
        return nullptr;
    };
    const auto* count = find("docp_count");
    REQUIRE(count != nullptr);
    CHECK_FALSE(count->documentation.has_value()); // not fetched for the list
    REQUIRE(count->data.has_value());

    auto resolved = CompletionProvider::resolve(*count, f.sdb);
    CHECK(docString(resolved.documentation) == "How many.");

    const auto* ext = find("docp_ext");
    REQUIRE(ext != nullptr);
    CHECK(docString(CompletionProvider::resolve(*ext, f.sdb).documentation) ==
          "Documented on the body.");

    // An undocumented symbol (`run`, line 17 col 16) resolves with no doc.
    lsp::CompletionItem run = *count;
    run.label = "run";
    auto& data = run.data->object();
    data["line"] = lsp::json::Integer(17);
    data["col"]  = lsp::json::Integer(16);
    data["kind"] = lsp::json::String("Function");
    CHECK_FALSE(CompletionProvider::resolve(run, f.sdb).documentation.has_value());
}

TEST_CASE("resolving an item without data returns it unchanged",
          "[lsp][completion][phase6.31]") {
    Fixture f;
    lsp::CompletionItem item;
    item.label = "begin";
    auto resolved = CompletionProvider::resolve(item, f.sdb);
    CHECK(resolved.label == "begin");
    CHECK_FALSE(resolved.documentation.has_value());
}
