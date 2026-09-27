#include <catch2/catch_test_macros.hpp>
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "db/project_compiler.h"
#include "compiler/parse_record.h"
#include "compiler/project_config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

// ---------------------------------------------------------------------------
// plan.md §6.19 piece 2 — attaching a prebuilt library DB read-only and
// querying across it (attach-and-query, no physical merge).
// ---------------------------------------------------------------------------

namespace fs = std::filesystem;

namespace {

const std::string kRoot = "/tmp/svlsp_test_library_attach";

// Builds a real, file-backed library DB at `path` (a fresh connection, then
// closed on return) containing `records` under file `libFilePath`. A
// *file-backed* DB is required here, not ":memory:" -- ATTACH DATABASE
// reads a path from disk, and an in-memory DB isn't nameable/shareable
// across two separate Database connections the way a real file is.
void buildLibraryDb(const std::string& path, const std::string& libFilePath,
                    const std::vector<ParseRecord>& records) {
    fs::create_directories(fs::path(path).parent_path());
    fs::remove(path);
    Database db(path);
    db.initSchema();
    SymbolDatabase sdb(db);
    auto fid = sdb.upsertFile(libFilePath, "h");
    sdb.replaceSymbols(fid, records);
}

bool hasSymbol(const std::vector<SymbolRow>& rows, const std::string& name) {
    return std::any_of(rows.begin(), rows.end(),
                       [&](const SymbolRow& r) { return r.name == name; });
}

struct ProjectFixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};
    ProjectFixture() { db.initSchema(); }
};

} // namespace

TEST_CASE("attachLibraryDbs: findSymbolsByName sees a symbol only in the attached DB",
          "[db][symbol-db][library-attach]")
{
    std::string libPath = kRoot + "/name/lib.db";
    buildLibraryDb(libPath, "/lib/uvm_object.sv",
        {{ParseRecordKind::Class, "uvm_object", 1, 6, "", "", 20, ""}});

    ProjectFixture f;
    f.sdb.attachLibraryDbs({libPath});

    auto rows = f.sdb.findSymbolsByName("uvm_object");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].kind == "Class");
    CHECK(rows[0].filePath == "/lib/uvm_object.sv");
}

TEST_CASE("attachLibraryDbs: findSymbolsByNamePrefix sees an attached-DB symbol",
          "[db][symbol-db][library-attach]")
{
    std::string libPath = kRoot + "/prefix/lib.db";
    buildLibraryDb(libPath, "/lib/uvm_object.sv",
        {{ParseRecordKind::Class, "uvm_object", 1, 6, "", "", 20, ""}});

    ProjectFixture f;
    f.sdb.attachLibraryDbs({libPath});

    auto rows = f.sdb.findSymbolsByNamePrefix("uvm_obj");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "uvm_object");
}

TEST_CASE("attachLibraryDbs: findSymbolsInScope sees an attached-DB class's own members",
          "[db][symbol-db][library-attach]")
{
    std::string libPath = kRoot + "/scope/lib.db";
    buildLibraryDb(libPath, "/lib/uvm_object.sv", {
        {ParseRecordKind::Class,    "uvm_object", 1, 6, "",           "", 20, ""},
        {ParseRecordKind::Function, "get_name",   5, 4, "uvm_object", "", 0,  "uvm_object"},
    });

    ProjectFixture f;
    f.sdb.attachLibraryDbs({libPath});

    auto rows = f.sdb.findSymbolsInScope("uvm_object");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "get_name");
}

TEST_CASE("attachLibraryDbs: findSymbolsVisibleAt sees an attached-DB top-level symbol"
          " with no import needed",
          "[db][symbol-db][library-attach]")
{
    std::string libPath = kRoot + "/visible-toplevel/lib.db";
    buildLibraryDb(libPath, "/lib/uvm_object.sv",
        {{ParseRecordKind::Class, "uvm_object", 1, 6, "", "", 20, ""}});

    ProjectFixture f;
    f.sdb.attachLibraryDbs({libPath});
    f.sdb.replaceSymbols(f.sdb.upsertFile("/proj/top.sv", "h"),
        {{ParseRecordKind::Module, "top", 1, 7, "", "", 10, ""}});

    auto rows = f.sdb.findSymbolsVisibleAt("/proj/top.sv", 5);
    CHECK(hasSymbol(rows, "top"));
    CHECK(hasSymbol(rows, "uvm_object"));
}

TEST_CASE("attachLibraryDbs: findSymbolsVisibleAt sees an attached-DB package's contents"
          " via a wildcard import",
          "[db][symbol-db][library-attach]")
{
    std::string libPath = kRoot + "/visible-import/lib.db";
    buildLibraryDb(libPath, "/lib/uvm_pkg.sv", {
        {ParseRecordKind::Package, "uvm_pkg",    1, 8, "",        "", 20, ""},
        {ParseRecordKind::Class,   "uvm_object", 2, 6, "uvm_pkg", "", 15, "uvm_pkg"},
    });

    ProjectFixture f;
    f.sdb.attachLibraryDbs({libPath});
    auto fid = f.sdb.upsertFile("/proj/top.sv", "h");
    f.sdb.replaceSymbols(fid,
        {{ParseRecordKind::Module, "top", 5, 7, "", "", 10, ""}});
    f.sdb.replaceImports(fid, {{"uvm_pkg", "*", 1}});

    auto rows = f.sdb.findSymbolsVisibleAt("/proj/top.sv", 7);
    CHECK(hasSymbol(rows, "uvm_object"));
    // uvm_pkg itself (scope "") is also visible as a top-level symbol,
    // independent of the import -- not what this test is about, but
    // confirms the fixture is wired up as expected.
    CHECK(hasSymbol(rows, "uvm_pkg"));
}

TEST_CASE("attachLibraryDbs: a path already attached is not re-attached under a second alias",
          "[db][symbol-db][library-attach]")
{
    std::string libPath = kRoot + "/dedup/lib.db";
    buildLibraryDb(libPath, "/lib/uvm_object.sv",
        {{ParseRecordKind::Class, "uvm_object", 1, 6, "", "", 20, ""}});

    ProjectFixture f;
    f.sdb.attachLibraryDbs({libPath});
    f.sdb.attachLibraryDbs({libPath}); // same path again, e.g. a second project sharing it

    auto rows = f.sdb.findSymbolsByName("uvm_object");
    REQUIRE(rows.size() == 1); // not 2
}

TEST_CASE("attachLibraryDbs: symbols from two distinct attached DBs are both visible",
          "[db][symbol-db][library-attach]")
{
    std::string libPathA = kRoot + "/multi/a.db";
    std::string libPathB = kRoot + "/multi/b.db";
    buildLibraryDb(libPathA, "/lib/a.sv",
        {{ParseRecordKind::Class, "class_a", 1, 6, "", "", 5, ""}});
    buildLibraryDb(libPathB, "/lib/b.sv",
        {{ParseRecordKind::Class, "class_b", 1, 6, "", "", 5, ""}});

    ProjectFixture f;
    f.sdb.attachLibraryDbs({libPathA, libPathB});

    CHECK(f.sdb.findSymbolsByName("class_a").size() == 1);
    CHECK(f.sdb.findSymbolsByName("class_b").size() == 1);
}

TEST_CASE("ProjectCompiler::loadProject attaches config.libraryDbs before compiling",
          "[db][project-compiler][library-attach]")
{
    std::string libPath = kRoot + "/via-project-compiler/lib.db";
    buildLibraryDb(libPath, "/lib/uvm_object.sv",
        {{ParseRecordKind::Class, "uvm_object", 1, 6, "", "", 20, ""}});

    std::string projFile = kRoot + "/via-project-compiler/top.sv";
    fs::create_directories(fs::path(projFile).parent_path());
    { std::ofstream f(projFile); f << "module top; endmodule\n"; }

    ProjectFixture f;
    CompilationController ctrl(f.sdb);
    ProjectConfig config;
    config.files      = {projFile};
    config.libraryDbs = {libPath};

    int compiled = ProjectCompiler::loadProject(config, ctrl, f.sdb);

    CHECK(compiled == 1); // the library DB isn't "compiled", just attached
    CHECK(f.sdb.findSymbolsByName("uvm_object").size() == 1);
    CHECK(f.sdb.findSymbolsByName("top").size() == 1);
}

TEST_CASE("attachLibraryDbs: findMacros sees an attached DB's macros, and skips a library "
          "built before the macros table existed (plan.md §6.29 part A)",
          "[db][symbol-db][library-attach][phase6.29]")
{
    std::string libPath = kRoot + "/macros/lib.db";
    std::string oldLibPath = kRoot + "/macros/old_lib.db";
    buildLibraryDb(libPath, "/lib/uvm_macros.svh", {});
    {
        Database db(libPath);
        SymbolDatabase sdb(db);
        sdb.replaceMacros(sdb.upsertFile("/lib/uvm_macros.svh", "h"),
                          {MacroRecord{"uvm_info", "", 155, 8, "", true,
                                       {"ID", "MSG", "VERBOSITY"}, {std::nullopt, std::nullopt,
                                                                    std::nullopt}}});
    }
    buildLibraryDb(oldLibPath, "/old/x.sv", {});
    {
        Database db(oldLibPath);
        db.execute("DROP TABLE macros");
    }

    ProjectFixture f;
    f.sdb.replaceMacros(f.sdb.upsertFile("/p/top.sv", "h"), {MacroRecord{"TOP", "1", 1, 8}});
    f.sdb.attachLibraryDbs({oldLibPath, libPath});

    auto rows = f.sdb.findMacros("uvm_info");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].filePath == "/lib/uvm_macros.svh");
    CHECK(rows[0].params.size() == 3);
    CHECK(f.sdb.findMacros("TOP").size() == 1);
}

TEST_CASE("attachLibraryDbs: findMacros reads a schema-v9 library's macros with an empty body",
          "[db][symbol-db][library-attach][phase6.29]")
{
    std::string libPath = kRoot + "/macros_v9/lib.db";
    buildLibraryDb(libPath, "/lib/m.svh", {});
    {
        Database db(libPath);
        SymbolDatabase sdb(db);
        sdb.replaceMacros(sdb.upsertFile("/lib/m.svh", "h"),
                          {MacroRecord{"V9M", "body", 3, 8, "", true, {"A"}, {std::nullopt}}});
        // Reshape into v9: macros without a body column.
        db.execute("CREATE TABLE m9 AS SELECT id, file_id, name, line, col, is_function_like, "
                   "params FROM macros; DROP TABLE macros; ALTER TABLE m9 RENAME TO macros;");
    }

    ProjectFixture f;
    f.sdb.attachLibraryDbs({libPath});
    auto rows = f.sdb.findMacros("V9M");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].params == std::vector<std::string>{"A"});
    CHECK(rows[0].body.empty());
}

TEST_CASE("attachLibraryDbs: findMacrosByNamePrefix sees an attached DB's macros",
          "[db][symbol-db][library-attach][workspace_symbols]")
{
    std::string libPath = kRoot + "/macros_prefix/lib.db";
    buildLibraryDb(libPath, "/lib/uvm_macros.svh", {});
    {
        Database db(libPath);
        SymbolDatabase sdb(db);
        sdb.replaceMacros(sdb.upsertFile("/lib/uvm_macros.svh", "h"),
                          {MacroRecord{"uvm_info", "", 155, 8}});
    }
    ProjectFixture f;
    f.sdb.replaceMacros(f.sdb.upsertFile("/p/top.sv", "h"), {MacroRecord{"uvm_top_m", "1", 1, 8}});
    f.sdb.attachLibraryDbs({libPath});

    auto rows = f.sdb.findMacrosByNamePrefix("uvm_");
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].name == "uvm_info");
    CHECK(rows[0].filePath == "/lib/uvm_macros.svh");
    CHECK(rows[1].name == "uvm_top_m");
}
