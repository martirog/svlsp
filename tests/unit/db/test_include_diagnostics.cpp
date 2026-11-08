// Include errors as diagnostics (plan.md §6.35 P2): under a project config
// an `include the preprocessor couldn't follow is an 'include' diagnostic
// on the file that holds the directive, over the directive. Error for not
// found / a cycle / the depth limit / unreadable, Warning for a malformed
// directive (I1); nothing for a file compiled outside a project (I2).

#include <catch2/catch_test_macros.hpp>
#include "db/project_refresher.h"
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "lsp/symbol_resolution.h"
#include "compiler/project_config.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ID {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    std::ostringstream    log;
    CompilationController ctrl{sdb, &log};
    ProjectConfig         config;
    std::string           dir;
    const std::string     key = "/project/.svlsp.json";
    std::vector<std::string> included, refreshed;

    explicit ID(const std::string& name) : dir("/tmp/svlsp_test_include_diags/" + name)
    {
        db.initSchema();
        ctrl.setTypeChecker(checkTypeRefs);
        ctrl.setCalleeResolver(resolveCallees);
        fs::remove_all(dir);
        fs::create_directories(dir);
        config.includeDirs = {dir};
    }
    // Writes `rel` with an mtime well in the past (stat data trusted).
    std::string write(const std::string& rel, const std::string& text, int secondsAgo = 60)
    {
        const std::string p = dir + "/" + rel;
        fs::create_directories(fs::path(p).parent_path());
        std::ofstream(p) << text;
        fs::last_write_time(p, fs::file_time_type::clock::now() -
                                   std::chrono::seconds(secondsAgo));
        return p;
    }
    std::string file(const std::string& rel) { return dir + "/" + rel; }
    std::string read(const std::string& rel)
    {
        std::ifstream in(file(rel));
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    // compile() of `rel` from disk, under the config unless `project` is false.
    std::vector<ParseError> compile(const std::string& rel, bool project = true)
    {
        return ctrl.compile(file(rel), read(rel), project ? &config : nullptr, &included, false,
                            &refreshed);
    }
    // The stored 'include' rows of `rel`.
    std::vector<DiagnosticRow> rows(const std::string& rel)
    {
        std::vector<DiagnosticRow> out;
        for (auto& d : sdb.diagnosticsForFile(file(rel)))
            if (d.source == "include") out.push_back(d);
        return out;
    }
    static std::vector<ParseError> includeErrors(const std::vector<ParseError>& errs)
    {
        std::vector<ParseError> out;
        for (const auto& e : errs)
            if (e.message.rfind("`include: ", 0) == 0) out.push_back(e);
        return out;
    }
    bool listed(const std::vector<std::string>& v, const std::string& rel)
    {
        return std::find(v.begin(), v.end(), file(rel)) != v.end();
    }
    ProjectRefresher::Result refresh()
    {
        config.files = {file("p.sv")};
        return ProjectRefresher::refresh(key, config, ctrl, sdb);
    }
};

int len(const std::string& s) { return static_cast<int>(s.size()); }

} // namespace

TEST_CASE("include diagnostics: a missing include is an Error over its directive",
          "[db][include-diags]")
{
    ID f("missing");
    const std::string dir = "`include \"id_missing.svh\"";
    f.write("p.sv", "package id_p;\n  " + dir + "  // why\nendpackage\n");
    const auto errs = ID::includeErrors(f.compile("p.sv"));
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: cannot find \"id_missing.svh\"");
    CHECK(errs[0].severity == 1);
    CHECK(errs[0].line == 2);
    CHECK(errs[0].column == 2);
    CHECK(errs[0].endColumn == 2 + len(dir));
    CHECK(errs[0].file.empty());

    const auto rows = f.rows("p.sv");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].line == 2);
    CHECK(rows[0].col == 2);
    CHECK(rows[0].endCol == 2 + len(dir));
    CHECK(rows[0].severity == 1);
}

TEST_CASE("include diagnostics: none for a file compiled outside a project",
          "[db][include-diags]")
{
    ID f("no-project");
    f.write("p.sv", "package id_np;\n`include \"id_missing.svh\"\nendpackage\n");
    CHECK(ID::includeErrors(f.compile("p.sv", false)).empty());
    CHECK(f.rows("p.sv").empty());
}

TEST_CASE("include diagnostics: a malformed directive is a Warning", "[db][include-diags]")
{
    ID f("malformed");
    f.write("p.sv", "package id_mf;\n`include id_bare.svh\nendpackage\n");
    const auto errs = ID::includeErrors(f.compile("p.sv"));
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: missing or malformed filename");
    CHECK(errs[0].severity == 2);
    CHECK(errs[0].endColumn == len("`include id_bare.svh"));
}

TEST_CASE("include diagnostics: a missing include inside an included file is on that file",
          "[db][include-diags]")
{
    ID f("nested");
    f.write("inner.svh", "class id_inner_c; endclass\n`include \"id_deeper.svh\"\n");
    f.write("p.sv", "package id_ns;\n`include \"inner.svh\"\nendpackage\n");
    const auto errs = f.compile("p.sv");
    CHECK(ID::includeErrors(errs).empty()); // not the primary's
    CHECK(f.rows("p.sv").empty());
    const auto rows = f.rows("inner.svh");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].message == "`include: cannot find \"id_deeper.svh\"");
    CHECK(rows[0].line == 2);
    CHECK(rows[0].col == 0);
    CHECK(rows[0].endCol == len("`include \"id_deeper.svh\""));
    // The server republishes the files a compile reports.
    CHECK(f.listed(f.included, "inner.svh"));
}

TEST_CASE("include diagnostics: a file holding nothing but a failing include is reported",
          "[db][include-diags]")
{
    ID f("wrapper");
    f.write("wrap.svh", "`include \"id_target.svh\"\n");
    f.write("p.sv", "package id_wr;\n`include \"wrap.svh\"\nendpackage\n");
    f.compile("p.sv");
    REQUIRE(f.rows("wrap.svh").size() == 1);
    CHECK(f.listed(f.included, "wrap.svh"));

    // Once the target exists, the row goes and the file is reported again,
    // so the client clears it.
    f.write("id_target.svh", "class id_target_c; endclass\n", 30);
    f.compile("p.sv");
    CHECK(f.rows("wrap.svh").empty());
    CHECK((f.listed(f.included, "wrap.svh") || f.listed(f.refreshed, "wrap.svh")));
}

TEST_CASE("include diagnostics: a cycle and the depth limit are Errors on the right file",
          "[db][include-diags]")
{
    SECTION("cycle")
    {
        ID f("cycle");
        f.write("a.svh", "class id_a; endclass\n`include \"b.svh\"\n");
        f.write("b.svh", "class id_b; endclass\n`include \"a.svh\"\n");
        f.write("p.sv", "package id_cy;\n`include \"a.svh\"\nendpackage\n");
        f.compile("p.sv");
        const auto rows = f.rows("b.svh");
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].message.find("cycle detected") != std::string::npos);
        CHECK(rows[0].severity == 1);
        CHECK(f.rows("a.svh").empty());
    }
    SECTION("depth")
    {
        ID f("depth");
        for (int k = 0; k < 20; ++k)
            f.write("d" + std::to_string(k) + ".svh",
                    "`include \"d" + std::to_string(k + 1) + ".svh\"\n");
        f.write("p.sv", "package id_dp;\n`include \"d0.svh\"\nendpackage\n");
        f.compile("p.sv");
        const auto rows = f.rows("d16.svh");
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].message == "`include: depth limit exceeded (cycle?)");
        CHECK(rows[0].severity == 1);
    }
}

TEST_CASE("include diagnostics: a cache hit still reports them, with their range",
          "[db][include-diags]")
{
    ID f("cache-hit");
    f.write("p.sv", "package id_ch;\n`include \"id_missing.svh\"\nendpackage\n");
    const auto first = ID::includeErrors(f.compile("p.sv"));
    REQUIRE(first.size() == 1);
    f.log.str("");
    const auto second = ID::includeErrors(f.compile("p.sv"));
    REQUIRE(f.log.str().find("(cached)") != std::string::npos);
    REQUIRE(second.size() == 1);
    CHECK(second[0].line == first[0].line);
    CHECK(second[0].column == first[0].column);
    CHECK(second[0].endColumn == first[0].endColumn);
    CHECK(second[0].severity == 1);
}

// The §6.34 S2 case that motivated §6.35: an include of a file that isn't
// there shows the error; the file appearing (a refresh) clears it, and
// removing it again brings it back.
TEST_CASE("include diagnostics: a refresh clears them when the file appears and restores them",
          "[db][include-diags][refresh]")
{
    ID f("refresh");
    f.write("p.sv", "package id_rf;\n`include \"id_comes.svh\"\nendpackage\n");
    f.refresh();
    REQUIRE(f.rows("p.sv").size() == 1);

    f.write("id_comes.svh", "class id_comes_c; endclass\n", 30);
    auto r = f.refresh();
    CHECK(f.rows("p.sv").empty());
    CHECK(!f.sdb.findSymbolsByName("id_comes_c").empty());
    CHECK(std::find(r.changedDiagnostics.begin(), r.changedDiagnostics.end(), f.file("p.sv")) !=
          r.changedDiagnostics.end());

    fs::remove(f.file("id_comes.svh"));
    r = f.refresh();
    REQUIRE(f.rows("p.sv").size() == 1);
    CHECK(f.rows("p.sv")[0].message == "`include: cannot find \"id_comes.svh\"");
    CHECK(std::find(r.changedDiagnostics.begin(), r.changedDiagnostics.end(), f.file("p.sv")) !=
          r.changedDiagnostics.end());
}
