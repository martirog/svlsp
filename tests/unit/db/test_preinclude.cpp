// Pre-includes (plan.md §6.40 D8): the config's `preincludes` are read
// before every unit's own text, like `gcc -include` -- a simulator compiles
// a filelist as one unit, so a macro defined in an earlier file is visible
// in later ones, and some projects never include uvm_macros.svh where they
// use it. Each header is read as an `include at the start of the unit:
// its macros are defined, its records are the header's own (stored once,
// not per unit), and editing it recompiles the units.

#include <catch2/catch_test_macros.hpp>
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "lsp/symbol_resolution.h"
#include "compiler/project_config.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct PI {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    std::ostringstream    log;
    CompilationController ctrl{sdb, &log};
    ProjectConfig         config;
    std::string           dir;
    std::vector<std::string> included, refreshed;

    explicit PI(const std::string& name) : dir("/tmp/svlsp_test_preinclude/" + name)
    {
        db.initSchema();
        ctrl.setTypeChecker(checkTypeRefs);
        ctrl.setCalleeResolver(resolveCallees);
        fs::remove_all(dir);
        fs::create_directories(dir + "/inc");
        config.includeDirs = {dir + "/inc"};
    }
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
    std::vector<ParseError> compile(const std::string& rel)
    {
        log.str("");
        return ctrl.compile(file(rel), read(rel), &config, &included, false, &refreshed);
    }
    bool cached() const { return log.str().find("(cached)") != std::string::npos; }
};

std::string messages(const std::vector<ParseError>& errs)
{
    std::string out;
    for (const auto& e : errs) out += std::to_string(e.line) + ": " + e.message + "\n";
    return out;
}

} // namespace

TEST_CASE("preinclude: a macro only the pre-include defines is defined in every unit",
          "[db][preinclude]")
{
    PI f("macro");
    f.write("inc/pi_macros.svh", "`define PI_INFO(m) $display(m)\n");
    f.write("a.sv", "module pi_a;\n  initial `PI_INFO(\"a\");\nendmodule\n");
    f.write("b.sv", "module pi_b;\n  initial `PI_INFO(\"b\");\nendmodule\n");

    SECTION("without it: undefined")
    {
        const auto errs = f.compile("a.sv");
        CHECK(messages(errs).find("PI_INFO") != std::string::npos);
    }
    SECTION("with it, through the include dirs: clean, lines unchanged")
    {
        f.config.preincludes = {"pi_macros.svh"};
        INFO(messages(f.compile("a.sv")));
        CHECK(f.compile("a.sv").empty());
        CHECK(f.compile("b.sv").empty());
        // A unit's own positions are its own lines.
        f.write("c.sv", "module pi_c;\n  int x = ;\nendmodule\n");
        const auto errs = f.compile("c.sv");
        REQUIRE(!errs.empty());
        CHECK(errs[0].line == 2);
        CHECK(errs[0].file.empty());
    }
    SECTION("with it, as an absolute path")
    {
        f.config.preincludes = {f.file("inc/pi_macros.svh")};
        CHECK(f.compile("a.sv").empty());
    }
}

TEST_CASE("preinclude: the header's records are its own, stored once", "[db][preinclude]")
{
    PI f("records");
    f.write("inc/pi_hdr.svh", "`define PI_W 8\ntypedef logic [`PI_W-1:0] pi_byte_t;\n");
    f.write("a.sv", "module pi_ra; pi_byte_t x; endmodule\n");
    f.write("b.sv", "module pi_rb; pi_byte_t y; endmodule\n");
    f.config.preincludes = {"pi_hdr.svh"};
    f.compile("a.sv");
    f.compile("b.sv");
    const auto syms = f.sdb.findSymbolsByName("pi_byte_t");
    REQUIRE(syms.size() == 1);
    CHECK(syms[0].filePath == f.file("inc/pi_hdr.svh"));
    CHECK(f.sdb.findMacros("PI_W").size() == 1);
}

TEST_CASE("preinclude: editing the header or the list recompiles", "[db][preinclude]")
{
    PI f("recompile");
    f.write("inc/pi_m.svh", "`define PI_V 1\n");
    f.write("a.sv", "module pi_ca; int v = `PI_V; endmodule\n");
    f.config.preincludes = {"pi_m.svh"};
    CHECK(f.compile("a.sv").empty());
    f.compile("a.sv");
    CHECK(f.cached());

    // The header no longer defines it.
    f.write("inc/pi_m.svh", "`define PI_OTHER 1\n", 30);
    const auto errs = f.compile("a.sv");
    CHECK(!f.cached());
    CHECK(messages(errs).find("PI_V") != std::string::npos);

    // Taken off the list.
    f.write("inc/pi_m.svh", "`define PI_V 1\n", 20);
    CHECK(f.compile("a.sv").empty());
    f.config.preincludes.clear();
    f.compile("a.sv");
    CHECK(!f.cached());
}

TEST_CASE("preinclude: one that can't be found is skipped, not an error in each unit",
          "[db][preinclude]")
{
    PI f("missing");
    f.write("a.sv", "module pi_ma; endmodule\n");
    f.config.preincludes = {"pi_nowhere.svh"};
    CHECK(f.compile("a.sv").empty());
    // It appearing recompiles the unit (it is an input that didn't resolve).
    f.compile("a.sv");
    CHECK(f.cached());
    f.write("inc/pi_nowhere.svh", "`define PI_NW\n", 30);
    f.compile("a.sv");
    CHECK(!f.cached());
}
