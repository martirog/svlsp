#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"
#include "lsp/library_db_builder.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "db/project_compiler.h"
#include "compiler/project_config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

// ---------------------------------------------------------------------------
// plan.md §6.19 — CompletionProvider (not just SymbolDatabase directly, see
// test_symbol_database_library_attach.cpp for that layer) with *two*
// separately built, independently attached library DBs, driven through a
// real compile pipeline rather than hand-inserted rows.
//
// Every case below deliberately compiles the project's own file twice:
// once with minimal, no-usage content (what a real didOpen would see), and
// only *after* a second, edited compile (the equivalent of a real
// didChange/live-edit recompile) does it ever call
// CompletionProvider::getCompletion. Asking for completions against the
// first compile's content would prove nothing here -- there is no usage of
// either library's classes in it yet for completion to resolve. The
// second compile's text is what actually exercises cross-DB resolution.
// ---------------------------------------------------------------------------

namespace fs = std::filesystem;

namespace {

const std::string kRoot = "/tmp/svlsp_test_completion_library_attach";

void writeFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

lsp::CompletionParams makeParams(const std::string& path, unsigned line, unsigned col) {
    lsp::CompletionParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

bool hasItem(const lsp::Array<lsp::CompletionItem>& items, std::string_view name) {
    return std::any_of(items.begin(), items.end(),
                       [&](const auto& i){ return i.label == name; });
}

struct TwoLibraryProject {
    Database              db{":memory:"};
    SymbolDatabase         sdb{db};
    CompilationController  ctrl{sdb};
    ProjectConfig          config;
    std::string            topPath;

    // Builds two real, independent library DBs on disk (each via
    // LibraryDbBuilder::build(), the same engine `svlsp --build-db` uses --
    // not synthetic rows), attaches both to `sdb`, and compiles `top.sv`
    // for the first time with minimal, no-usage content.
    TwoLibraryProject() {
        db.initSchema();

        std::string root = kRoot + "/" + std::to_string(reinterpret_cast<uintptr_t>(this));

        writeFile(root + "/libA/netpacket.sv",
            "class NetPacket;\n"
            "  function int checksum();\n"
            "    return 0;\n"
            "  endfunction\n"
            "endclass\n");
        writeFile(root + "/libA/netpacket.f", "netpacket.sv\n");
        std::string libADb = root + "/libA/lib.db";
        fs::remove(libADb);
        auto resultA = LibraryDbBuilder::build(root + "/libA/netpacket.f", libADb);
        REQUIRE(resultA.ok);

        writeFile(root + "/libB/authtoken.sv",
            "class AuthToken;\n"
            "  function int expiresAt();\n"
            "    return 0;\n"
            "  endfunction\n"
            "endclass\n");
        writeFile(root + "/libB/authtoken.f", "authtoken.sv\n");
        std::string libBDb = root + "/libB/lib.db";
        fs::remove(libBDb);
        auto resultB = LibraryDbBuilder::build(root + "/libB/authtoken.f", libBDb);
        REQUIRE(resultB.ok);

        topPath = root + "/proj/top.sv";
        writeFile(topPath, "module top;\nendmodule\n");

        config.files      = {topPath};
        config.libraryDbs = {libADb, libBDb};

        // The first compile -- deliberately no usage of either library's
        // classes yet, and deliberately never queried for completions.
        int compiled = ProjectCompiler::loadProject(config, ctrl, sdb);
        REQUIRE(compiled == 1);
    }

    // Recompiles top.sv with `text` (the "live edit"/didChange equivalent) --
    // callers pass the same text straight into
    // CompletionProvider::getCompletion afterward.
    void liveEdit(const std::string& text) {
        ctrl.compile(topPath, text, &config);
    }
};

} // namespace

TEST_CASE("CompletionProvider: dot-completion resolves a class from each of two"
          " independently attached library DBs",
          "[completion][library-attach]")
{
    TwoLibraryProject proj;

    // The live edit: declares both libraries' classes and probes each on a
    // "probe: ..." line inside a never-defined `ifdef (the preprocessor
    // drops it, so the file stays valid SV; completion reads the raw text,
    // and a comment would be skipped as prose) -- this text did not exist
    // at the first, no-usage compile above.
    const std::string text =
        "module top;\n"
        "  NetPacket pkt;\n"
        "  AuthToken tok;\n"
        "`ifdef SVLSP_TEST_PROBES\n"
        "     probe: pkt.\n"
        "     probe: tok.\n"
        "`endif\n"
        "endmodule\n";
    proj.liveEdit(text);

    auto pktResult = CompletionProvider::getCompletion(makeParams(proj.topPath, 4, 16), proj.sdb, text);
    REQUIRE_FALSE(pktResult.isNull());
    auto& pktItems = pktResult.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(pktItems, "checksum"));    // from libA
    CHECK_FALSE(hasItem(pktItems, "expiresAt")); // not leaked from libB

    auto tokResult = CompletionProvider::getCompletion(makeParams(proj.topPath, 5, 16), proj.sdb, text);
    REQUIRE_FALSE(tokResult.isNull());
    auto& tokItems = tokResult.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(tokItems, "expiresAt"));   // from libB
    CHECK_FALSE(hasItem(tokItems, "checksum")); // not leaked from libA
}

TEST_CASE("CompletionProvider: a fuzzy top-level prefix reaches class names in"
          " either of two attached library DBs",
          "[completion][library-attach]")
{
    TwoLibraryProject proj;

    // Same live-edit discipline as above: each prefix is typed into a real
    // recompile of top.sv (on a "probe: ..." line inside a never-defined
    // `ifdef, so the file stays syntactically valid throughout) rather than
    // queried against the first compile's no-usage content.
    const std::string netText =
        "module top;\n"
        "`ifdef SVLSP_TEST_PROBES\n"
        "     probe: Net\n"
        "`endif\n"
        "endmodule\n";
    proj.liveEdit(netText);
    auto netResult = CompletionProvider::getCompletion(makeParams(proj.topPath, 2, 15), proj.sdb, netText);
    REQUIRE_FALSE(netResult.isNull());
    CHECK(hasItem(netResult.get<lsp::Array<lsp::CompletionItem>>(), "NetPacket"));

    const std::string authText =
        "module top;\n"
        "`ifdef SVLSP_TEST_PROBES\n"
        "     probe: Auth\n"
        "`endif\n"
        "endmodule\n";
    proj.liveEdit(authText);
    auto authResult = CompletionProvider::getCompletion(makeParams(proj.topPath, 2, 16), proj.sdb, authText);
    REQUIRE_FALSE(authResult.isNull());
    CHECK(hasItem(authResult.get<lsp::Array<lsp::CompletionItem>>(), "AuthToken"));
}
