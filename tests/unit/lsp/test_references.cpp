#include <catch2/catch_test_macros.hpp>
#include "lsp/references.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <functional>
#include <map>
#include <optional>

namespace {

struct Fixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};

    Fixture() { db.initSchema(); }
};

lsp::ReferenceParams makeParams(std::string_view path, unsigned line, unsigned col,
                                 bool includeDeclaration = false)
{
    lsp::ReferenceParams p;
    p.textDocument.uri            = lsp::DocumentUri::fromPath(path);
    p.position.line               = line;
    p.position.character          = col;
    p.context.includeDeclaration  = includeDeclaration;
    return p;
}

// A simple in-memory {path -> text} lookup standing in for
// LanguageServer::currentTextFor (open-buffer-or-disk) in these unit tests.
std::function<std::optional<std::string>(const std::string&)> textMapLookup(
    const std::map<std::string, std::string>& files)
{
    return [&files](const std::string& path) -> std::optional<std::string> {
        auto it = files.find(path);
        if (it == files.end()) return std::nullopt;
        return it->second;
    };
}

} // namespace

TEST_CASE("ReferencesProvider: null when cursor is not on an identifier", "[references]")
{
    Fixture f;
    const std::string text = "  module top;";
    auto result = ReferencesProvider::getReferences(
        makeParams("/t.sv", 0, 0), f.sdb, text, textMapLookup({}));
    REQUIRE(result.isNull());
}

TEST_CASE("ReferencesProvider: null when identifier is not a known symbol", "[references]")
{
    Fixture f;
    const std::string text = "module unknown_sym;";
    auto result = ReferencesProvider::getReferences(
        makeParams("/t.sv", 0, 7), f.sdb, text, textMapLookup({}));
    REQUIRE(result.isNull());
}

TEST_CASE("ReferencesProvider: finds every use of a signal in the same file, excluding "
          "the declaration by default", "[references]")
{
    Fixture f;
    const std::string text =
        "module top;\n"
        "  logic data;\n"
        "  assign data = 1;\n"
        "  assign data = data;\n"
        "endmodule\n";
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Signal, "data", 2, 8, "", "", 0, ""}});

    std::map<std::string, std::string> files{{"/t.sv", text}};
    // Cursor on the declaration itself (line 1, col 8).
    auto result = ReferencesProvider::getReferences(
        makeParams("/t.sv", 1, 8), f.sdb, text, textMapLookup(files));
    REQUIRE_FALSE(result.isNull());
    // Three uses after the declaration ("data = 1", "data = data" x2), the
    // declaration itself excluded.
    CHECK(result.value().size() == 3);
}

TEST_CASE("ReferencesProvider: includes the declaration when includeDeclaration is true",
          "[references]")
{
    Fixture f;
    const std::string text =
        "module top;\n"
        "  logic data;\n"
        "  assign data = 1;\n"
        "endmodule\n";
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Signal, "data", 2, 8, "", "", 0, ""}});

    std::map<std::string, std::string> files{{"/t.sv", text}};
    auto result = ReferencesProvider::getReferences(
        makeParams("/t.sv", 1, 8, /*includeDeclaration=*/true), f.sdb, text, textMapLookup(files));
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().size() == 2); // declaration + the one use
}

TEST_CASE("ReferencesProvider: finds uses across every file the DB knows about",
          "[references]")
{
    Fixture f;
    const std::string declText = "module m; logic shared_sig; endmodule\n";
    const std::string useText  = "module consumer; assign x = shared_sig; endmodule\n";

    auto declFid = f.sdb.upsertFile("/decl.sv", "h1");
    f.sdb.replaceSymbols(declFid, {{ParseRecordKind::Signal, "shared_sig", 1, 16, "", "", 0, ""}});
    f.sdb.upsertFile("/use.sv", "h2");

    std::map<std::string, std::string> files{{"/decl.sv", declText}, {"/use.sv", useText}};
    auto result = ReferencesProvider::getReferences(
        makeParams("/decl.sv", 0, 16, /*includeDeclaration=*/true), f.sdb, declText,
        textMapLookup(files));
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().size() == 2); // declaration in decl.sv + use in use.sv
}

TEST_CASE("ReferencesProvider: does not match a same-named identifier inside a comment "
          "or string literal", "[references]")
{
    Fixture f;
    const std::string text =
        "module top;\n"
        "  logic data;\n"
        "  // data is not really used here\n"
        "  assign data = 1; // data\n"
        "  string s = \"data\";\n"
        "endmodule\n";
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Signal, "data", 2, 8, "", "", 0, ""}});

    std::map<std::string, std::string> files{{"/t.sv", text}};
    auto result = ReferencesProvider::getReferences(
        makeParams("/t.sv", 1, 8, /*includeDeclaration=*/true), f.sdb, text, textMapLookup(files));
    REQUIRE_FALSE(result.isNull());
    // Declaration + the one real assignment use -- both comment occurrences
    // and the one inside the string literal must be excluded.
    CHECK(result.value().size() == 2);
}

TEST_CASE("ReferencesProvider: skips a file whose current text is unavailable",
          "[references]")
{
    Fixture f;
    const std::string declText = "module m; logic shared_sig; endmodule\n";

    auto declFid = f.sdb.upsertFile("/decl.sv", "h1");
    f.sdb.replaceSymbols(declFid, {{ParseRecordKind::Signal, "shared_sig", 1, 16, "", "", 0, ""}});
    f.sdb.upsertFile("/deleted.sv", "h2"); // known to the DB, but not in the text map

    std::map<std::string, std::string> files{{"/decl.sv", declText}};
    auto result = ReferencesProvider::getReferences(
        makeParams("/decl.sv", 0, 16, /*includeDeclaration=*/true), f.sdb, declText,
        textMapLookup(files));
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().size() == 1); // just the declaration -- no crash on /deleted.sv
}
