#include <catch2/catch_test_macros.hpp>
#include "lsp/references.h"
#include "lsp/rename.h"
#include "lsp/symbol_resolution.h"
#include <lsp/error.h>
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <tuple>

// References from a project into an attached library DB (plan.md §6.33
// C10): the library's files are searched too. Rename never edits a library,
// so it fails closed on a symbol a library declares.

namespace fs = std::filesystem;

namespace {

const std::string kLibPath = "/lib/lib_pkg.sv";
const std::string kTbPath  = "/proj/tb.sv";

const std::string kLibText =
    "package lib_pkg;\n"
    "  `define LIB_TAG(x) x\n"
    "  class base;\n"
    "    function int get_id(); return 1; endfunction\n"
    "    function int twice(); return get_id() * 2; endfunction\n"
    "    function int tag(); return `LIB_TAG(4); endfunction\n"
    "  endclass\n"
    "endpackage\n";

const std::string kTbText =
    "module tb;\n"
    "  import lib_pkg::*;\n"
    "  base b;\n"
    "  initial begin\n"
    "    int x = b.get_id();\n"
    "    int y = `LIB_TAG(3);\n"
    "  end\n"
    "endmodule\n";

// 0-based {line, character} of the `nth` (0-based) occurrence of `needle`.
std::pair<unsigned, unsigned> posOf(const std::string& text, const std::string& needle,
                                    int nth = 0)
{
    size_t off = text.find(needle);
    for (int i = 0; i < nth; ++i) off = text.find(needle, off + 1);
    REQUIRE(off != std::string::npos);
    unsigned line = 0;
    size_t lineStart = 0;
    for (size_t i = 0; i < off; ++i)
        if (text[i] == '\n') { ++line; lineStart = i + 1; }
    return {line, static_cast<unsigned>(off - lineStart)};
}

// A library DB built from kLibText (file-backed: ATTACH reads a path), and
// a project DB with kTbText compiled against it attached.
struct Fixture {
    std::string libDb = (fs::temp_directory_path() / "svlsp_test_refs_library/lib.db").string();
    Database       db{":memory:"};
    SymbolDatabase sdb{db};
    std::string tbText;
    std::map<std::string, std::string> files;
    std::map<std::string, int> reads; // textFor() calls, by path

    explicit Fixture(std::string tb = kTbText)
        : tbText{std::move(tb)}, files{{kLibPath, kLibText}, {kTbPath, tbText}}
    {
        fs::create_directories(fs::path(libDb).parent_path());
        fs::remove(libDb);
        {
            Database lib(libDb);
            lib.initSchema();
            SymbolDatabase libSdb(lib);
            CompilationController c(libSdb);
            c.setCalleeResolver(resolveCallees);
            c.compile(kLibPath, kLibText);
        }
        db.initSchema();
        sdb.attachLibraryDbs({libDb});
        CompilationController c(sdb);
        c.setCalleeResolver(resolveCallees);
        c.compile(kTbPath, tbText);
    }

    std::function<std::optional<std::string>(const std::string&)> textFor()
    {
        return [this](const std::string& path) -> std::optional<std::string> {
            ++reads[path];
            auto it = files.find(path);
            if (it == files.end()) return std::nullopt;
            return it->second;
        };
    }
};

using Loc = std::tuple<std::string, unsigned, unsigned>;

std::set<Loc> referencesAt(Fixture& f, std::pair<unsigned, unsigned> pos)
{
    lsp::ReferenceParams p;
    p.textDocument.uri           = lsp::DocumentUri::fromPath(kTbPath);
    p.position.line              = pos.first;
    p.position.character         = pos.second;
    p.context.includeDeclaration = true;
    const auto result = ReferencesProvider::getReferences(p, f.sdb, f.tbText, f.textFor());
    std::set<Loc> out;
    if (result.isNull()) return out;
    for (const auto& loc : result.value())
        out.insert({std::string(loc.uri.path()), loc.range.start.line, loc.range.start.character});
    return out;
}

Loc at(const std::string& path, std::pair<unsigned, unsigned> pos)
{
    return {path, pos.first, pos.second};
}

} // namespace

TEST_CASE("libraryFilePaths lists an attached library DB's files",
          "[references][library-attach]")
{
    Fixture f;
    CHECK(f.sdb.allFilePaths() == std::vector<std::string>{kTbPath});
    CHECK(f.sdb.libraryFilePaths() == std::vector<std::string>{kLibPath});
}

TEST_CASE("references from a project find a library method's declaration and uses",
          "[references][library-attach]")
{
    Fixture f;
    const auto refs = referencesAt(f, posOf(kTbText, "get_id"));
    CHECK(refs == std::set<Loc>{
        at(kTbPath,  posOf(kTbText, "get_id")),
        at(kLibPath, posOf(kLibText, "get_id")),     // the declaration
        at(kLibPath, posOf(kLibText, "get_id", 1)),  // twice()'s call
    });
}

TEST_CASE("references from a project find a library macro's definition and uses",
          "[references][library-attach]")
{
    Fixture f;
    const auto refs = referencesAt(f, posOf(kTbText, "LIB_TAG"));
    CHECK(refs == std::set<Loc>{
        at(kTbPath,  posOf(kTbText, "LIB_TAG")),
        at(kLibPath, posOf(kLibText, "LIB_TAG")),     // the `define
        at(kLibPath, posOf(kLibText, "LIB_TAG", 1)),  // tag()'s use
    });
}

lsp::TextDocument_RenameResult renameAt(Fixture& f, std::pair<unsigned, unsigned> pos,
                                        const std::string& newName = "renamed")
{
    lsp::RenameParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(kTbPath);
    p.position.line      = pos.first;
    p.position.character = pos.second;
    p.newName            = newName;
    return RenameProvider::getRename(p, f.sdb, f.tbText, f.textFor());
}

// The message of the error renameAt throws, or "" if it doesn't.
std::string renameError(Fixture& f, std::pair<unsigned, unsigned> pos)
{
    try {
        renameAt(f, pos);
    } catch (const lsp::RequestError& e) {
        CHECK(e.code() == lsp::MessageError::RequestFailed);
        return e.message();
    }
    return "";
}

TEST_CASE("rename of a library method fails closed instead of renaming only the project's uses",
          "[rename][library-attach]")
{
    Fixture f;
    const auto msg = renameError(f, posOf(kTbText, "get_id"));
    CHECK(msg.find("'get_id'") != std::string::npos);
    CHECK(msg.find(kLibPath) != std::string::npos);
}

TEST_CASE("rename of a library macro fails closed", "[rename][library-attach]")
{
    Fixture f;
    CHECK(renameError(f, posOf(kTbText, "LIB_TAG")).find(kLibPath) != std::string::npos);
}

TEST_CASE("rename of a project override of a library method fails closed",
          "[rename][library-attach]")
{
    // Renaming the override alone would stop it overriding.
    const std::string tb =
        "module tb;\n"
        "  import lib_pkg::*;\n"
        "  class ext extends base;\n"
        "    function int get_id(); return 2; endfunction\n"
        "  endclass\n"
        "endmodule\n";
    Fixture f{tb};
    CHECK(renameError(f, posOf(tb, "get_id")).find(kLibPath) != std::string::npos);
}

TEST_CASE("rename of a project symbol with a library attached edits the project only",
          "[rename][library-attach]")
{
    const std::string tb =
        "module tb;\n"
        "  int get_id;\n"
        "  initial get_id = 1;\n"
        "endmodule\n";
    Fixture f{tb};
    const auto result = renameAt(f, posOf(tb, "get_id"), "get_key");
    REQUIRE_FALSE(result.isNull());
    REQUIRE(result.value().changes);
    REQUIRE(result.value().changes->size() == 1);
    const auto& [uri, edits] = *result.value().changes->begin();
    CHECK(std::string(uri.path()) == kTbPath);
    CHECK(edits.size() == 2);
    CHECK(f.reads[kLibPath] == 0);
}

TEST_CASE("references to a project override reach the library method it overrides",
          "[references][library-attach]")
{
    const std::string tb =
        "module tb;\n"
        "  import lib_pkg::*;\n"
        "  class ext extends base;\n"
        "    function int get_id(); return 2; endfunction\n"
        "  endclass\n"
        "endmodule\n";
    Fixture f{tb};
    const auto refs = referencesAt(f, posOf(tb, "get_id"));
    CHECK(refs == std::set<Loc>{
        at(kTbPath,  posOf(tb, "get_id")),
        at(kLibPath, posOf(kLibText, "get_id")),
        at(kLibPath, posOf(kLibText, "get_id", 1)),
    });
}

TEST_CASE("references to a project-only symbol don't read the library's files",
          "[references][library-attach]")
{
    // A library can't refer to what only the project declares.
    const std::string tb =
        "module tb;\n"
        "  int get_id;\n"
        "  initial get_id = 1;\n"
        "endmodule\n";
    Fixture f{tb};
    const auto refs = referencesAt(f, posOf(tb, "get_id"));
    CHECK(refs == std::set<Loc>{at(kTbPath, posOf(tb, "get_id")),
                                at(kTbPath, posOf(tb, "get_id", 1))});
    CHECK(f.reads[kLibPath] == 0);
}
