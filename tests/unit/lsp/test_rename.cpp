#include <catch2/catch_test_macros.hpp>
#include "lsp/rename.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <functional>
#include <lsp/error.h>
#include <map>
#include <optional>

namespace {

struct Fixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};

    Fixture() { db.initSchema(); }
};

lsp::RenameParams makeParams(std::string_view path, unsigned line, unsigned col,
                              std::string_view newName)
{
    lsp::RenameParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    p.newName            = newName;
    return p;
}

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

TEST_CASE("RenameProvider: null when cursor is not on an identifier", "[rename]")
{
    Fixture f;
    const std::string text = "  module top;";
    auto result = RenameProvider::getRename(
        makeParams("/t.sv", 0, 0, "new_name"), f.sdb, text, textMapLookup({}));
    REQUIRE(result.isNull());
}

TEST_CASE("RenameProvider: null when identifier is not a known symbol", "[rename]")
{
    Fixture f;
    const std::string text = "module unknown_sym;";
    auto result = RenameProvider::getRename(
        makeParams("/t.sv", 0, 7, "renamed"), f.sdb, text, textMapLookup({}));
    REQUIRE(result.isNull());
}

TEST_CASE("RenameProvider: throws InvalidParams for an illegal new identifier", "[rename]")
{
    Fixture f;
    const std::string text = "module top; logic data; endmodule\n";
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Signal, "data", 1, 19, "", "", 0, ""}});

    std::map<std::string, std::string> files{{"/t.sv", text}};
    CHECK_THROWS_AS(
        RenameProvider::getRename(
            makeParams("/t.sv", 0, 19, "1_not_legal"), f.sdb, text, textMapLookup(files)),
        lsp::RequestError);
}

TEST_CASE("RenameProvider: rejects a name containing invalid characters", "[rename]")
{
    Fixture f;
    const std::string text = "module top; logic data; endmodule\n";
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Signal, "data", 1, 19, "", "", 0, ""}});

    std::map<std::string, std::string> files{{"/t.sv", text}};
    CHECK_THROWS_AS(
        RenameProvider::getRename(
            makeParams("/t.sv", 0, 19, "not-legal"), f.sdb, text, textMapLookup(files)),
        lsp::RequestError);
}

TEST_CASE("RenameProvider: builds a WorkspaceEdit renaming every occurrence, "
          "including the declaration", "[rename]")
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
    auto result = RenameProvider::getRename(
        makeParams("/t.sv", 1, 8, "value"), f.sdb, text, textMapLookup(files));
    REQUIRE_FALSE(result.isNull());

    const auto& edit = result.value();
    REQUIRE(edit.changes.has_value());
    REQUIRE(edit.changes->size() == 1);
    const auto& edits = edit.changes->begin()->second;
    REQUIRE(edits.size() == 2); // declaration + the one use
    for (const auto& te : edits)
        CHECK(te.newText == "value");
}

TEST_CASE("RenameProvider: edits span every file the DB knows about", "[rename]")
{
    Fixture f;
    const std::string declText = "module m; logic shared_sig; endmodule\n";
    const std::string useText  = "module consumer; assign x = shared_sig; endmodule\n";

    auto declFid = f.sdb.upsertFile("/decl.sv", "h1");
    f.sdb.replaceSymbols(declFid, {{ParseRecordKind::Signal, "shared_sig", 1, 16, "", "", 0, ""}});
    f.sdb.upsertFile("/use.sv", "h2");

    std::map<std::string, std::string> files{{"/decl.sv", declText}, {"/use.sv", useText}};
    auto result = RenameProvider::getRename(
        makeParams("/decl.sv", 0, 16, "renamed_sig"), f.sdb, declText, textMapLookup(files));
    REQUIRE_FALSE(result.isNull());

    const auto& edit = result.value();
    REQUIRE(edit.changes.has_value());
    CHECK(edit.changes->size() == 2); // one edit list per file
}
