#include <catch2/catch_test_macros.hpp>
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "compiler/project_config.h"
#include <algorithm>
#include <fstream>

// ---------------------------------------------------------------------------
// Phase 5.4 — CompilationController (DB-backed incremental compilation)
//
// Observable invariants:
//   • First compile: pipeline runs, symbols + diagnostics stored in DB.
//   • Same content again: DB hash matches → pipeline skipped (symbols
//     unchanged — verified by injecting a sentinel row between calls).
//   • Changed content: hash mismatch → pipeline re-runs, DB updated.
// ---------------------------------------------------------------------------

// Helper: produce a controller backed by an in-memory database.
namespace {
struct Fixture {
    Database              db;
    SymbolDatabase        sdb;
    CompilationController ctrl;

    Fixture()
        : db(":memory:")
        , sdb(db)
        , ctrl(sdb)
    {
        db.initSchema();
    }
};
}

TEST_CASE("first compile of valid source stores symbols in DB", "[db][ctrl]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "module top; endmodule\n");

    auto syms = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(!syms.empty());
    CHECK(syms[0].name == "top");
}

TEST_CASE("first compile stores file hash in DB", "[db][ctrl]") {
    Fixture f;
    const std::string src = "module top; endmodule\n";
    f.ctrl.compile("/a.sv", src);
    CHECK(!f.sdb.getFileHash("/a.sv").empty());
}

TEST_CASE("compile of valid source returns no errors", "[db][ctrl]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv", "module top; endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("compile of malformed source returns parse errors", "[db][ctrl]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv", "module bad { endmodule\n");
    CHECK(!errs.empty());
}

TEST_CASE("compile stores diagnostics in DB", "[db][ctrl]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "module bad { endmodule\n");
    auto diags = f.sdb.diagnosticsForFile("/a.sv");
    CHECK(!diags.empty());
}

TEST_CASE("second compile with same content skips pipeline — symbols unchanged",
          "[db][ctrl]") {
    Fixture f;
    const std::string src = "module top; endmodule\n";
    f.ctrl.compile("/a.sv", src);

    // Inject a sentinel symbol directly into the DB.
    auto fid = f.sdb.upsertFile("/a.sv", f.sdb.getFileHash("/a.sv"));
    f.db.execute("INSERT INTO symbols (file_id,kind,name,line,col) VALUES ("
                 + std::to_string(fid) + ",'Module','__sentinel__',99,0)");

    // Same content → cache hit → replaceSymbols NOT called → sentinel survives.
    f.ctrl.compile("/a.sv", src);
    auto syms = f.sdb.symbolsForFile("/a.sv");
    bool found = false;
    for (const auto& s : syms)
        if (s.name == "__sentinel__") { found = true; break; }
    CHECK(found);
}

TEST_CASE("second compile with changed content re-runs pipeline", "[db][ctrl]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "module old_name; endmodule\n");

    // Verify old symbol present
    auto before = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(before.size() == 1);
    CHECK(before[0].name == "old_name");

    // Change content → hash mismatch → pipeline re-runs
    f.ctrl.compile("/a.sv", "module new_name; endmodule\n");

    auto after = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(after.size() == 1);
    CHECK(after[0].name == "new_name");
}

TEST_CASE("compile returns errors from DB on cache hit", "[db][ctrl]") {
    Fixture f;
    const std::string bad = "module bad { endmodule\n";
    auto errs1 = f.ctrl.compile("/a.sv", bad);
    REQUIRE(!errs1.empty());

    // Same content → cache hit → errors come from DB
    auto errs2 = f.ctrl.compile("/a.sv", bad);
    REQUIRE(!errs2.empty());
    CHECK(errs1[0].line    == errs2[0].line);
    CHECK(errs1[0].message == errs2[0].message);
}

TEST_CASE("independent files are cached independently", "[db][ctrl]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "module a_mod; endmodule\n");
    f.ctrl.compile("/b.sv", "module b_mod; endmodule\n");

    auto a = f.sdb.symbolsForFile("/a.sv");
    auto b = f.sdb.symbolsForFile("/b.sv");
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0].name == "a_mod");
    CHECK(b[0].name == "b_mod");
}

TEST_CASE("symbols from included file are stored under included file path",
          "[db][ctrl]") {
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_inc.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; endmodule\n"; }

    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";
    f.ctrl.compile("/main.sv", src);

    auto incSyms = f.sdb.symbolsForFile(incPath);
    REQUIRE(incSyms.size() == 1);
    CHECK(incSyms[0].name == "from_include");

    auto mainSyms = f.sdb.symbolsForFile("/main.sv");
    REQUIRE(mainSyms.size() == 1);
    CHECK(mainSyms[0].name == "main_mod");
}

TEST_CASE("compile persists an instantiation as unresolved when its type isn't declared",
          "[db][ctrl][instantiation]") {
    Fixture f;
    f.ctrl.compile("/top.sv", "module top; sub u0(); endmodule\n");

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(std::find(unresolved.begin(), unresolved.end(), "sub") != unresolved.end());
}

TEST_CASE("compile persists an instantiation attributed to the correct file",
          "[db][ctrl][instantiation]") {
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_inc_inst.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; sub u0(); endmodule\n"; }

    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";
    f.ctrl.compile("/main.sv", src);

    auto incFid = f.sdb.upsertFile(incPath, "");
    auto stmt = f.db.prepare(
        "SELECT type_name, inst_name FROM instantiations WHERE file_id = ?");
    stmt.bind(1, incFid);
    REQUIRE(stmt.step());
    CHECK(stmt.columnText(0) == "sub");
    CHECK(stmt.columnText(1) == "u0");
}

TEST_CASE("compile resolves an instantiation once its type is declared elsewhere",
          "[db][ctrl][instantiation]") {
    Fixture f;
    f.ctrl.compile("/sub.sv", "module sub; endmodule\n");
    f.ctrl.compile("/top.sv", "module top; sub u0(); endmodule\n");

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(std::find(unresolved.begin(), unresolved.end(), "sub") == unresolved.end());
}

TEST_CASE("compile with a config define gates an ifdef", "[db][ctrl][project-config]") {
    Fixture f;
    ProjectConfig config;
    config.defines["SIM"] = "";

    f.ctrl.compile("/a.sv", "`ifdef SIM\nmodule sim_only; endmodule\n`endif\n", &config);

    auto syms = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(syms.size() == 1);
    CHECK(syms[0].name == "sim_only");
}

TEST_CASE("compile without a config define leaves the ifdef block inactive",
          "[db][ctrl][project-config]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "`ifdef SIM\nmodule sim_only; endmodule\n`endif\n");

    auto syms = f.sdb.symbolsForFile("/a.sv");
    CHECK(syms.empty());
}

TEST_CASE("compile with a config includeDir resolves a bare `include", "[db][ctrl][project-config]") {
    Fixture f;
    std::string incPath = "/tmp/svlsp_test_ctrl_cfg_inc.sv";
    { std::ofstream ofs(incPath); ofs << "module via_config_incdir; endmodule\n"; }

    ProjectConfig config;
    config.includeDirs.push_back("/tmp");

    f.ctrl.compile("/a.sv", "`include \"svlsp_test_ctrl_cfg_inc.sv\"\n", &config);

    auto syms = f.sdb.symbolsForFile(incPath);
    REQUIRE(syms.size() == 1);
    CHECK(syms[0].name == "via_config_incdir");
}
