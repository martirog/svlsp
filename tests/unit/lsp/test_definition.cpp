#include <catch2/catch_test_macros.hpp>
#include "lsp/definition.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"

namespace {

struct Fixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};

    Fixture() { db.initSchema(); }
};

lsp::DefinitionParams makeParams(std::string_view path, unsigned line, unsigned col) {
    lsp::DefinitionParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

} // namespace

TEST_CASE("DefinitionProvider: null when cursor is not on an identifier", "[definition]")
{
    Fixture f;
    auto result = DefinitionProvider::getDefinition(makeParams("/t.sv", 0, 0), f.sdb, "  ");
    REQUIRE(result.isNull());
}

TEST_CASE("DefinitionProvider: null when identifier not in DB", "[definition]")
{
    Fixture f;
    auto result = DefinitionProvider::getDefinition(
        makeParams("/t.sv", 0, 0), f.sdb, "unknown_sym");
    REQUIRE(result.isNull());
}

TEST_CASE("DefinitionProvider: returns Location for known symbol", "[definition]")
{
    Fixture f;
    const std::string defPath = "/src/adder.sv";
    int64_t fid = f.sdb.upsertFile(defPath, "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "adder", 4, 7, "", "", 20, ""},
    });

    // Hovering on "adder" in some consumer file
    const std::string text = "adder u1(";
    auto result = DefinitionProvider::getDefinition(makeParams("/top.sv", 0, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    REQUIRE(result.holdsAlternative<lsp::Definition>());
    const auto& def = result.get<lsp::Definition>();
    REQUIRE(std::holds_alternative<lsp::Location>(def));
    const auto& loc = std::get<lsp::Location>(def);

    // URI should point to definition file
    CHECK(std::string(loc.uri.path()) == defPath);
    // 1-based line 4 → 0-based line 3
    CHECK(loc.range.start.line      == 3u);
    CHECK(loc.range.start.character == 7u);
}

TEST_CASE("DefinitionProvider: cross-file definition lookup", "[definition]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/pkg.sv",  "h"),
        {{ParseRecordKind::Class, "MyClass", 10, 5, "my_pkg", "", 30, "my_pkg"}});
    f.sdb.replaceSymbols(f.sdb.upsertFile("/top.sv",  "h"),
        {{ParseRecordKind::Module, "top", 1, 7, "", "", 50, ""}});

    // In top.sv, cursor on "MyClass"
    const std::string text = "MyClass obj;";
    auto result = DefinitionProvider::getDefinition(makeParams("/top.sv", 0, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& loc = std::get<lsp::Location>(result.get<lsp::Definition>());
    CHECK(std::string(loc.uri.path()) == "/pkg.sv");
    CHECK(loc.range.start.line == 9u); // 1-based 10 → 0-based 9
}
