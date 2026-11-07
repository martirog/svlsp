// ProjectRefresher (plan.md §6.34 S2): a refresh recompiles only the units
// whose inputs changed, removes what is gone, and re-runs the cross-file
// checks of every unit when declarations changed (D3).

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

struct RF {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    std::ostringstream    log;
    CompilationController ctrl{sdb, &log};
    ProjectConfig         config;
    std::string           dir;
    const std::string     key = "/project/.svlsp.json";

    explicit RF(const std::string& name) : dir("/tmp/svlsp_test_refresher/" + name)
    {
        db.initSchema();
        ctrl.setTypeChecker(checkTypeRefs);
        ctrl.setCalleeResolver(resolveCallees);
        fs::remove_all(dir);
        fs::create_directories(dir);
        config.includeDirs = {dir};
    }
    // Writes `rel` with an mtime well in the past, so its stat data is
    // trusted (not racy).
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
    void list(std::initializer_list<const char*> rels)
    {
        config.files.clear();
        for (const char* r : rels) config.files.push_back(file(r));
    }
    ProjectRefresher::Plan plan() { return ProjectRefresher::plan(key, config, ctrl, sdb); }
    ProjectRefresher::Result refresh()
    {
        log.str("");
        return ProjectRefresher::refresh(key, config, ctrl, sdb);
    }
    // The units the last refresh parsed (not cache hits).
    std::vector<std::string> parsed()
    {
        std::vector<std::string> out;
        std::istringstream in(log.str());
        for (std::string line; std::getline(in, line);)
            if (line.rfind("[parsed] ", 0) == 0 && line.find("(cached)") == std::string::npos &&
                line.find("included:") == std::string::npos)
                out.push_back(line.substr(9));
        return out;
    }
    bool has(const std::string& name) { return !sdb.findSymbolsByName(name).empty(); }
    std::vector<std::string> messages(const std::string& path)
    {
        std::vector<std::string> out;
        for (const auto& d : sdb.diagnosticsForFile(path)) out.push_back(d.message);
        return out;
    }
    bool hasMessage(const std::string& path, const std::string& needle)
    {
        const auto m = messages(path);
        return std::any_of(m.begin(), m.end(),
                           [&](const std::string& s) { return s.find(needle) != std::string::npos; });
    }
};

} // namespace

TEST_CASE("refresh: the first one compiles every unit; an unchanged project compiles none",
          "[db][refresh]")
{
    RF f("first");
    f.write("a.sv", "package ra_p; class ra_c; endclass endpackage\n");
    f.write("b.sv", "module rb_m; endmodule\n");
    f.list({"a.sv", "b.sv"});
    const auto first = f.refresh();
    CHECK(first.compiled == 2);
    CHECK(f.has("ra_c"));
    CHECK(f.sdb.projectUnits(f.key).size() == 2);

    const auto p = f.plan();
    CHECK(p.compile.empty());
    CHECK(p.remove.empty());
    CHECK(p.rehashedFiles == 0); // stat data trusted: nothing read
    CHECK(f.refresh().compiled == 0);
}

TEST_CASE("refresh: touching one .svh affects exactly its includers", "[db][refresh]")
{
    RF f("touch-svh");
    f.write("shared.svh", "class rs_c; endclass\n");
    f.write("other.svh", "class ro_c; endclass\n");
    f.write("p1.sv", "package rp1;\n`include \"shared.svh\"\nendpackage\n");
    f.write("p2.sv", "package rp2;\n`include \"shared.svh\"\nendpackage\n");
    f.write("p3.sv", "package rp3;\n`include \"other.svh\"\nendpackage\n");
    f.list({"p1.sv", "p2.sv", "p3.sv"});
    f.refresh();

    f.write("shared.svh", "class rs_c; int x; endclass\n");
    const auto p = f.plan();
    CHECK(p.compile == std::vector<std::string>{f.file("p1.sv"), f.file("p2.sv")});
    f.refresh();
    CHECK(f.parsed() == std::vector<std::string>{f.file("p1.sv"), f.file("p2.sv")});
}

TEST_CASE("refresh: a stat-only touch with the same content recompiles nothing",
          "[db][refresh]")
{
    RF f("stat-touch");
    f.write("a.svh", "class rt_c; endclass\n");
    f.write("p.sv", "package rt_p;\n`include \"a.svh\"\nendpackage\n");
    f.list({"p.sv"});
    f.refresh();
    f.write("a.svh", "class rt_c; endclass\n", 30); // same text, new mtime
    auto p = f.plan();
    CHECK(p.rehashedFiles == 1);
    CHECK(p.compile.empty());
    // The new stat data was recorded: the next plan reads nothing.
    CHECK(f.plan().rehashedFiles == 0);
}

TEST_CASE("refresh: a define change makes every unit affected", "[db][refresh]")
{
    RF f("define");
    f.write("a.sv", "module rd_a; endmodule\n");
    f.write("b.sv", "module rd_b; endmodule\n");
    f.list({"a.sv", "b.sv"});
    f.refresh();
    f.config.defines["RD_NEW"] = "1";
    const auto p = f.plan();
    CHECK(p.configChanged);
    CHECK(p.compile.size() == 2);
}

TEST_CASE("refresh: a file dropped from the config loses its rows", "[db][refresh]")
{
    RF f("dropped");
    f.write("a.sv", "module rr_a; endmodule\n");
    f.write("b.sv", "module rr_b; endmodule\n");
    f.list({"a.sv", "b.sv"});
    f.refresh();
    REQUIRE(f.has("rr_b"));
    f.list({"a.sv"});
    const auto r = f.refresh();
    CHECK(r.removed == 1);
    CHECK(r.compiled == 0);
    CHECK_FALSE(f.has("rr_b"));
    CHECK(f.sdb.fileIdFor(f.file("b.sv")) < 0);
    CHECK(f.sdb.projectUnits(f.key).size() == 1);
}

TEST_CASE("refresh: a deleted included file's rows go and its includer recompiles",
          "[db][refresh]")
{
    RF f("deleted-include");
    f.write("gone.svh", "class rg_c; endclass\n");
    f.write("p.sv", "package rg_p;\n`include \"gone.svh\"\nendpackage\n");
    f.list({"p.sv"});
    f.refresh();
    REQUIRE(f.has("rg_c"));
    fs::remove(f.file("gone.svh"));
    const auto p = f.plan();
    CHECK(p.remove == std::vector<std::string>{f.file("gone.svh")});
    CHECK(p.compile == std::vector<std::string>{f.file("p.sv")});
    f.refresh();
    CHECK(f.parsed() == std::vector<std::string>{f.file("p.sv")});
    CHECK_FALSE(f.has("rg_c"));
    CHECK(f.sdb.fileIdFor(f.file("gone.svh")) < 0);
    // The include is recorded as unresolved: a later plan stays quiet until
    // the file is back (preprocessor include errors aren't diagnostics).
    CHECK(f.plan().compile.empty());
    f.write("gone.svh", "class rg_c; endclass\n");
    CHECK(f.plan().compile == std::vector<std::string>{f.file("p.sv")});
}

TEST_CASE("refresh: removing a class flags its uses in an unchanged file; restoring it clears them",
          "[db][refresh][d3]")
{
    RF f("d3-type");
    const std::string withClass = "package rc_p; class rc_c; endclass endpackage\n";
    f.write("pkg.sv", withClass);
    f.write("use.sv", "module rc_m;\n  import rc_p::*;\n  rc_c obj;\nendmodule\n");
    f.list({"pkg.sv", "use.sv"});
    f.refresh();
    REQUIRE_FALSE(f.hasMessage(f.file("use.sv"), "unknown type"));

    f.write("pkg.sv", "package rc_p; endpackage\n", 30);
    auto r = f.refresh();
    CHECK(f.parsed() == std::vector<std::string>{f.file("pkg.sv")}); // use.sv not parsed
    CHECK(r.rechecked);
    CHECK(f.hasMessage(f.file("use.sv"), "unknown type 'rc_c'"));
    CHECK(std::find(r.changedDiagnostics.begin(), r.changedDiagnostics.end(),
                    f.file("use.sv")) != r.changedDiagnostics.end());

    f.write("pkg.sv", withClass, 20);
    r = f.refresh();
    CHECK(r.rechecked);
    CHECK_FALSE(f.hasMessage(f.file("use.sv"), "unknown type"));
}

TEST_CASE("refresh: a callee that gains a required argument flags an unchanged caller",
          "[db][refresh][d3]")
{
    RF f("d3-call");
    f.write("pkg.sv", "package rk_p;\n  function void rk_f(int a); endfunction\nendpackage\n");
    f.write("use.sv",
            "module rk_m;\n  import rk_p::*;\n  initial rk_f(1);\nendmodule\n");
    f.list({"pkg.sv", "use.sv"});
    f.refresh();
    REQUIRE_FALSE(f.hasMessage(f.file("use.sv"), "missing required argument"));

    f.write("pkg.sv",
            "package rk_p;\n  function void rk_f(int a, int b); endfunction\nendpackage\n", 30);
    const auto r = f.refresh();
    CHECK(f.parsed() == std::vector<std::string>{f.file("pkg.sv")});
    CHECK(r.rechecked);
    CHECK(f.hasMessage(f.file("use.sv"), "missing required argument 'b'"));
}

TEST_CASE("refresh: one that changes no declaration runs no checks", "[db][refresh][d3]")
{
    RF f("d3-none");
    f.write("pkg.sv", "package rn_p;\n  function int rn_f(); return 1; endfunction\nendpackage\n");
    f.write("use.sv", "module rn_m; import rn_p::*; endmodule\n");
    f.list({"pkg.sv", "use.sv"});
    f.refresh();
    f.write("pkg.sv", "package rn_p;\n  function int rn_f(); return 2; endfunction\nendpackage\n",
            30);
    const auto r = f.refresh();
    CHECK(r.compiled == 1);
    CHECK_FALSE(r.rechecked);
    CHECK(f.log.str().find("[recheck]") == std::string::npos);
}

TEST_CASE("refresh: a config file that is now only `include'd is recompiled through its includer",
          "[db][refresh]")
{
    RF f("now-included");
    f.write("a.svh", "class ri_c; endclass\n");
    f.write("p.sv", "package ri_p; endpackage\n");
    f.list({"a.svh", "p.sv"});
    f.refresh();
    f.write("p.sv", "package ri_p;\n`include \"a.svh\"\nendpackage\n", 30);
    f.list({"p.sv"});
    f.refresh();
    const auto rows = f.sdb.findSymbolsByName("ri_c");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].scope == "ri_p"); // from p.sv's compile, not its own
}
