#include <catch2/catch_test_macros.hpp>
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "compiler/parse_record.h"
#include <filesystem>

// Doc comments in the DB (plan.md §6.31): the symbol_docs table and
// macros.doc, read back with SymbolDatabase::docFor / MacroRow::doc.

namespace fs = std::filesystem;

namespace {

struct Fixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};
    Fixture() { db.initSchema(); }
};

ParseRecord rec(const std::string& name, int line, int col, const std::string& doc) {
    ParseRecord r{ParseRecordKind::Signal, name, line, col, "", ""};
    r.doc = doc;
    return r;
}

const SymbolRow& only(const std::vector<SymbolRow>& rows) {
    REQUIRE(rows.size() == 1);
    return rows[0];
}

} // namespace

TEST_CASE("docFor returns a symbol's stored doc, and \"\" for an undocumented one",
          "[db][symbol-db][phase6.31]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/p/a.sv", "h");
    f.sdb.replaceSymbols(fid, {rec("documented", 3, 6, "Counts things."), rec("bare", 4, 6, "")});
    CHECK(f.sdb.docFor(only(f.sdb.findSymbolsByName("documented"))) == "Counts things.");
    CHECK(f.sdb.docFor(only(f.sdb.findSymbolsByName("bare"))).empty());
    auto n = f.db.prepare("SELECT COUNT(*) FROM symbol_docs");
    REQUIRE(n.step());
    CHECK(n.columnInt(0) == 1); // undocumented symbols store nothing
}

TEST_CASE("replaceSymbols replaces a file's docs", "[db][symbol-db][phase6.31]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/p/a.sv", "h");
    f.sdb.replaceSymbols(fid, {rec("x", 3, 6, "Old doc.")});
    f.sdb.replaceSymbols(fid, {rec("x", 3, 6, "New doc.")});
    CHECK(f.sdb.docFor(only(f.sdb.findSymbolsByName("x"))) == "New doc.");
    f.sdb.replaceSymbols(fid, {rec("x", 3, 6, "")});
    CHECK(f.sdb.docFor(only(f.sdb.findSymbolsByName("x"))).empty());
}

TEST_CASE("macro docs round-trip through the macros table", "[db][symbol-db][phase6.31]") {
    Fixture f;
    MacroRecord m{"DOC_W", "32", 3, 8};
    m.doc = "Max width.";
    f.sdb.replaceMacros(f.sdb.upsertFile("/p/a.sv", "h"), {m});
    auto rows = f.sdb.findMacros("DOC_W");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].doc == "Max width.");
}

TEST_CASE("docFor and macro docs read an attached library DB, with or without the tables",
          "[db][symbol-db][library-attach][phase6.31]") {
    const std::string root = "/tmp/svlsp_test_symbol_docs";
    fs::create_directories(root);
    const std::string libPath = root + "/lib.db", oldPath = root + "/old.db";
    for (const auto& p : {libPath, oldPath}) {
        fs::remove(p);
        Database db(p);
        db.initSchema();
        SymbolDatabase sdb(db);
        auto fid = sdb.upsertFile(p == libPath ? "/lib/a.sv" : "/old/a.sv", "h");
        sdb.replaceSymbols(fid, {rec(p == libPath ? "lib_sym" : "old_sym", 5, 2, "Library doc.")});
        MacroRecord m{p == libPath ? "LIB_M" : "OLD_M", "1", 1, 8};
        m.doc = "Macro doc.";
        sdb.replaceMacros(fid, {m});
    }
    {
        // Reshape into a v10 library: no symbol_docs, macros without doc.
        Database db(oldPath);
        db.execute("DROP TABLE symbol_docs;"
                   "CREATE TABLE m10 AS SELECT id, file_id, name, line, col, is_function_like, "
                   "params, body FROM macros; DROP TABLE macros; ALTER TABLE m10 RENAME TO macros;");
    }

    Fixture f;
    f.sdb.attachLibraryDbs({oldPath, libPath});
    CHECK(f.sdb.docFor(only(f.sdb.findSymbolsByName("lib_sym"))) == "Library doc.");
    CHECK(f.sdb.docFor(only(f.sdb.findSymbolsByName("old_sym"))).empty());
    auto lib = f.sdb.findMacros("LIB_M");
    REQUIRE(lib.size() == 1);
    CHECK(lib[0].doc == "Macro doc.");
    auto old = f.sdb.findMacros("OLD_M");
    REQUIRE(old.size() == 1);
    CHECK(old[0].doc.empty());
}

TEST_CASE("CompilationController stores docs, and none when collection is off",
          "[db][compilation][phase6.31]") {
    const std::string src =
        "// Documented module.\n"
        "module doc_ctl;\n"
        "endmodule\n"
        "// Documented macro.\n"
        "`define DOC_CTL_M 1\n";
    {
        Fixture f;
        CompilationController cc(f.sdb);
        cc.compile("/p/ctl.sv", src);
        CHECK(f.sdb.docFor(only(f.sdb.findSymbolsByName("doc_ctl"))) == "Documented module.");
        CHECK(f.sdb.findMacros("DOC_CTL_M").at(0).doc == "Documented macro.");
    }
    {
        Fixture f;
        CompilationController cc(f.sdb);
        cc.setCollectDocs(false);
        cc.compile("/p/ctl.sv", src);
        CHECK(f.sdb.docFor(only(f.sdb.findSymbolsByName("doc_ctl"))).empty());
        CHECK(f.sdb.findMacros("DOC_CTL_M").at(0).doc.empty());
        auto n = f.db.prepare("SELECT COUNT(*) FROM symbol_docs");
        REQUIRE(n.step());
        CHECK(n.columnInt(0) == 0);
    }
}
