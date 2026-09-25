#include <catch2/catch_test_macros.hpp>
#include "lsp/rename.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include "db/compilation_controller.h"
#include <functional>
#include <lsp/error.h>
#include <map>
#include <optional>
#include <set>
#include <tuple>

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

struct RealCompileFixture {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    CompilationController ctrl{sdb};
    std::map<std::string, std::string> files;

    RealCompileFixture() { db.initSchema(); }

    void add(const std::string& path, const std::string& text)
    {
        files[path] = text;
        ctrl.compile(path, text);
    }
};

using Edit = std::tuple<std::string, unsigned, unsigned>; // path, 0-based line, col

// Every edit's position; also checks each replaces exactly `oldLen` chars.
std::set<Edit> editPositions(const lsp::TextDocument_RenameResult& result, size_t oldLen)
{
    std::set<Edit> out;
    if (result.isNull() || !result.value().changes) return out;
    for (const auto& [uri, edits] : *result.value().changes)
        for (const auto& te : edits) {
            CHECK(te.range.end.character - te.range.start.character == oldLen);
            out.insert({std::string(uri.path()), te.range.start.line, te.range.start.character});
        }
    return out;
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

// Rename edits exactly what references reports (plan.md §6.30 step B): a
// same-named but unrelated declaration must be left alone -- renaming it
// too silently breaks code that never referred to the renamed symbol.
TEST_CASE("RenameProvider: leaves a same-named signal in another module untouched",
          "[rename][scoped]")
{
    RealCompileFixture f;
    f.add("/m.sv",
          "module rn_m1;\n"
          "  logic rn_clk;\n"
          "  assign rn_clk = 1'b0;\n"
          "endmodule\n"
          "module rn_m2;\n"
          "  logic rn_clk;\n"
          "  assign rn_clk = 1'b1;\n"
          "endmodule\n");

    auto result = RenameProvider::getRename(makeParams("/m.sv", 5, 8, "rn_clock"), f.sdb,
                                            f.files.at("/m.sv"), textMapLookup(f.files));
    CHECK(editPositions(result, 6) == std::set<Edit>{{"/m.sv", 5, 8}, {"/m.sv", 6, 9}});
}

TEST_CASE("RenameProvider: renames a virtual method with its overrides and calls, not an "
          "unrelated class's same-named method",
          "[rename][scoped]")
{
    RealCompileFixture f;
    f.add("/c.sv",
          "class RnBase;\n"
          "  virtual function void rn_run(); endfunction\n"
          "endclass\n"
          "class RnDerived extends RnBase;\n"
          "  virtual function void rn_run();\n"
          "    super.rn_run();\n"
          "  endfunction\n"
          "endclass\n"
          "class RnOther;\n"
          "  function void rn_run(); endfunction\n"
          "endclass\n");
    f.add("/use.sv",
          "module rn_top;\n"
          "  RnDerived d; RnOther o;\n"
          "  initial begin\n"
          "    d.rn_run();\n"
          "    o.rn_run();\n"
          "  end\n"
          "endmodule\n");

    // Cursor on the derived override; the base declaration comes along.
    auto result = RenameProvider::getRename(makeParams("/c.sv", 4, 24, "rn_exec"), f.sdb,
                                            f.files.at("/c.sv"), textMapLookup(f.files));
    CHECK(editPositions(result, 6) == std::set<Edit>{{"/c.sv", 1, 24},
                                                     {"/c.sv", 4, 24},
                                                     {"/c.sv", 5, 10},
                                                     {"/use.sv", 3, 6}});
}
