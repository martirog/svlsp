#include <catch2/catch_test_macros.hpp>
#include "compiler/sv_preprocessor.h"
#include "compiler/content_hash.h"
#include "compiler/file_utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Object-like macros
// ---------------------------------------------------------------------------

TEST_CASE("object-like macro expands", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`define WIDTH 8\nwire [`WIDTH-1:0] bus;\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out == "\nwire [8-1:0] bus;\n");
}

TEST_CASE("macro defined with no body expands to empty", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`define SIM\n`ifdef SIM\nwire a;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out == "\n\nwire a;\n\n");
}

TEST_CASE("undef removes macro", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define A 1\n"
        "`undef A\n"
        "`ifdef A\nwire a;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    // block is skipped because A was undefined
    REQUIRE(out.find("wire a;") == std::string::npos);
}

TEST_CASE("undefineall clears all macros", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define A 1\n"
        "`define B 2\n"
        "`undefineall\n"
        "`ifdef A\nwire a;\n`endif\n"
        "`ifdef B\nwire b;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire a;") == std::string::npos);
    REQUIRE(out.find("wire b;") == std::string::npos);
}

TEST_CASE("recursive macro expansion", "[compiler][preprocessor]") {
    // A's body contains a backtick invocation of B, so `A → `B → 42
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define A `B\n"
        "`define B 42\n"
        "wire [`A];\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire [42];") != std::string::npos);
}

TEST_CASE("undefined macro expands to empty and records error", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("wire [`UNDEF-1:0] bus;\n", "f.sv");
    REQUIRE(!errs.empty());
    // The undefined token is replaced with empty
    REQUIRE(out.find("`UNDEF") == std::string::npos);
}

TEST_CASE("predefined macro via define() is visible", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("CHIP_TOP", "my_chip");
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("wire w = `CHIP_TOP;\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("my_chip") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Function-like macros
// ---------------------------------------------------------------------------

TEST_CASE("function-like macro single argument", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define BITS(n) [n-1:0]\n"
        "wire `BITS(8) bus;\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire [8-1:0] bus;") != std::string::npos);
}

TEST_CASE("function-like macro multiple arguments", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define MAX(a,b) ((a)>(b)?(a):(b))\n"
        "assign x = `MAX(p, q);\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("((p)>(q)?(p):(q))") != std::string::npos);
}

TEST_CASE("function-like macro zero arguments", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define NOP() begin end\n"
        "`NOP()\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("begin end") != std::string::npos);
}

TEST_CASE("function-like macro parameter with default value used when omitted",
          "[compiler][preprocessor][defaultargs]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define GREET(NAME, GREETING=hello) GREETING NAME\n"
        "`GREET(world)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("hello world") != std::string::npos);
}

TEST_CASE("function-like macro parameter with default value overridden when supplied",
          "[compiler][preprocessor][defaultargs]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define GREET(NAME, GREETING=hello) GREETING NAME\n"
        "`GREET(world, hi)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("hi world") != std::string::npos);
}

TEST_CASE("function-like macro default value containing nested parens does not hang",
          "[compiler][preprocessor][defaultargs]") {
    // Mirrors real UVM: `define uvm_report_begin(SEVERITY, ID, VERBOSITY,
    // RO=uvm_get_report_object()) -- a default value that is itself a
    // function call, i.e. contains its own '(' and ')'.
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define M(A, B=default_call()) A B\n"
        "`M(x)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("x default_call()") != std::string::npos);
}

TEST_CASE("function-like macro missing required argument without default records error",
          "[compiler][preprocessor][defaultargs]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(x)\n", "f.sv");
    REQUIRE(!errs.empty());
}

TEST_CASE("function-like macro too many arguments still records error",
          "[compiler][preprocessor][defaultargs]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define M(A, B=default_val) A B\n"
        "`M(x, y, z)\n", "f.sv");
    REQUIRE(!errs.empty());
}

// ---------------------------------------------------------------------------
// Macro-argument parsing must track {}/[] nesting, and string-literal
// contents, not just ()
// ---------------------------------------------------------------------------

TEST_CASE("macro argument containing a brace expression with a comma is not split",
          "[compiler][preprocessor][bracenesting]") {
    // Mirrors real UVM: `uvm_warning(ID, {"part one ", part_two}) -- the
    // comma inside {...} must not be treated as the argument separator.
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(\"id\", {\"part one \", part_two})\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("\"id\" {\"part one \", part_two}") != std::string::npos);
}

TEST_CASE("macro argument containing a bracket expression with a comma is not split",
          "[compiler][preprocessor][bracenesting]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(\"id\", arr[i, j])\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("\"id\" arr[i, j]") != std::string::npos);
}

TEST_CASE("macro argument string literal containing a comma is not split",
          "[compiler][preprocessor][bracenesting]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(\"hello, world\", second)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("\"hello, world\" second") != std::string::npos);
}

TEST_CASE("macro argument string literal containing an unmatched paren is not split",
          "[compiler][preprocessor][bracenesting]") {
    // A quoted message string like "already exists (see above)" must not
    // perturb paren-depth tracking of the invocation itself.
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(\"already exists (see above)\", second)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("\"already exists (see above)\" second") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Comments must not be scanned for macro invocations
// ---------------------------------------------------------------------------

TEST_CASE("backtick-like text inside a line comment is not macro-expanded",
          "[compiler][preprocessor][comments]") {
    // Common in doc comments that show example macro usage, e.g. UVM's
    // "// |`uvm_info(ID, MSG, VERBOSITY)" -- must not be treated as an
    // actual invocation of an undefined (or wrong-arity) macro.
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "// example: `uvm_info(ID, MSG, VERBOSITY)\n"
        "wire a;\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("// example: `uvm_info(ID, MSG, VERBOSITY)") != std::string::npos);
}

TEST_CASE("macro invocation before a trailing comment still expands",
          "[compiler][preprocessor][comments]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define WIDTH 8\n"
        "wire [`WIDTH-1:0] bus; // uses `UNDEFINED_MACRO in the comment\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire [8-1:0] bus;") != std::string::npos);
    REQUIRE(out.find("// uses `UNDEFINED_MACRO in the comment") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Conditional compilation
// ---------------------------------------------------------------------------

TEST_CASE("ifdef defined — block included", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define SIM\n"
        "`ifdef SIM\nwire sim_wire;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_wire;") != std::string::npos);
}

TEST_CASE("ifdef undefined — block skipped", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`ifdef SIM\nwire sim_wire;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_wire;") == std::string::npos);
}

TEST_CASE("ifndef undefined — block included", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`ifndef SYNTHESIS\nwire sim_only;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_only;") != std::string::npos);
}

TEST_CASE("ifndef defined — block skipped", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("SYNTHESIS");
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`ifndef SYNTHESIS\nwire sim_only;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_only;") == std::string::npos);
}

TEST_CASE("else branch taken when ifdef false", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`ifdef SIM\nwire sim_wire;\n`else\nwire synth_wire;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_wire;") == std::string::npos);
    REQUIRE(out.find("wire synth_wire;") != std::string::npos);
}

TEST_CASE("elsif first branch taken", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("A");
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`ifdef A\nwire a;\n`elsif B\nwire b;\n`else\nwire c;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire a;") != std::string::npos);
    REQUIRE(out.find("wire b;") == std::string::npos);
    REQUIRE(out.find("wire c;") == std::string::npos);
}

TEST_CASE("elsif second branch taken", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("B");
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`ifdef A\nwire a;\n`elsif B\nwire b;\n`else\nwire c;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire a;") == std::string::npos);
    REQUIRE(out.find("wire b;") != std::string::npos);
    REQUIRE(out.find("wire c;") == std::string::npos);
}

TEST_CASE("nested ifdef", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("OUTER");
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`ifdef OUTER\n"
        "  `ifdef INNER\nwire inner;\n`endif\n"
        "wire outer;\n"
        "`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire inner;") == std::string::npos);
    REQUIRE(out.find("wire outer;") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Include
// ---------------------------------------------------------------------------

TEST_CASE("include inserts file content", "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_a.sv";
    { std::ofstream f(tmpPath); f << "wire from_include;\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire from_include;") != std::string::npos);
}

TEST_CASE("include propagates macro definitions", "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_b.sv";
    { std::ofstream f(tmpPath); f << "`define INC_WIDTH 16\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`include \"" + tmpPath + "\"\n"
        "wire [`INC_WIDTH-1:0] bus;\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire [16-1:0] bus;") != std::string::npos);
}

TEST_CASE("include via include path", "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_c.sv";
    { std::ofstream f(tmpPath); f << "wire via_path;\n"; }

    SvPreprocessor pp({"/tmp"});
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`include \"svlsp_test_inc_c.sv\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire via_path;") != std::string::npos);
}

TEST_CASE("include missing file records error", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`include \"no_such_file.sv\"\n", "test.sv");
    REQUIRE(!errs.empty());
}

// plan.md §6.34 S1: every file the preprocessor reads is reported with the
// identity it had when read -- the content hash of the text it read, and
// its size and mtime from a stat taken before reading -- so a compile can
// record what its result depends on.
TEST_CASE("each include site carries the identity of the file read",
          "[compiler][preprocessor][file-identity]") {
    const std::string inner = "/tmp/svlsp_test_ident_inner.svh";
    const std::string wrap  = "/tmp/svlsp_test_ident_wrap.svh";
    const std::string innerText = "wire from_inner;\n";
    const std::string wrapText  = "`include \"" + inner + "\"\n";
    { std::ofstream f(inner); f << innerText; }
    { std::ofstream f(wrap);  f << wrapText; }

    const auto before = nowNs();
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites, dirs_] = pp.process("`include \"" + wrap + "\"\n", "top.sv");
    REQUIRE(errs.empty());
    REQUIRE(sites.size() == 2);
    // The wrapper holds nothing but an `include, and is reported too.
    CHECK(sites[0].file == wrap);
    CHECK(sites[0].hash == contentHash(wrapText));
    CHECK(sites[0].size == static_cast<int64_t>(wrapText.size()));
    CHECK(sites[1].file == inner);
    CHECK(sites[1].hash == contentHash(innerText));
    CHECK(sites[1].mtimeNs == statFile(inner)->mtimeNs);
    CHECK(sites[1].statNs >= before);
}

TEST_CASE("each include directive is reported with what it resolved to",
          "[compiler][preprocessor][file-identity]") {
    const std::string inc = "/tmp/svlsp_test_ident_dir_a.svh";
    { std::ofstream f(inc); f << "wire a;\n"; }
    SvPreprocessor pp({"/tmp"});
    auto [out, errs, macros_, map_, sites_, dirs] = pp.process(
        "`include \"svlsp_test_ident_dir_a.svh\"\n"
        "`include \"svlsp_test_ident_dir_a.svh\"\n"   // the same directive: once
        "`include \"svlsp_test_no_such.svh\"\n", "/src/top.sv");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0].name == "svlsp_test_ident_dir_a.svh");
    CHECK(dirs[0].fromFile == "/src/top.sv");
    CHECK(dirs[0].resolved == inc);
    CHECK(dirs[1].name == "svlsp_test_no_such.svh");
    CHECK(dirs[1].resolved.empty());
    // The resolution the preprocessor used, on its own.
    CHECK(resolveInclude("svlsp_test_ident_dir_a.svh", "/src/top.sv", {"/tmp"}) == inc);
    CHECK(resolveInclude("svlsp_test_ident_dir_a.svh", "/src/top.sv", {}).empty());
}

TEST_CASE("a failed include's error is a not-found include error",
          "[compiler][preprocessor][file-identity]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`include \"no_such_file.svh\"\n", "t.sv");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].kind == PreprocessorError::Kind::IncludeNotFound);
    CHECK(sites_.empty());
}

// A fresh directory for the cwd-independence cases below, and a guard that
// puts the working directory back.
static std::filesystem::path freshIncludeDir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

namespace {
struct CwdGuard {
    std::filesystem::path saved = std::filesystem::current_path();
    ~CwdGuard() { std::filesystem::current_path(saved); }
};
}  // namespace

TEST_CASE("a relative include is looked up beside its includer, not in the working directory",
          "[compiler][preprocessor]") {
    // A file of the same relative name in the working directory must not
    // win (a library built from its own directory got cwd-relative paths).
    const auto root = freshIncludeDir("svlsp_test_inc_cwd");
    std::filesystem::create_directories(root / "lib/base");
    std::filesystem::create_directories(root / "cwd/base");
    { std::ofstream f(root / "lib/base/a.svh"); f << "wire from_lib;\n"; }
    { std::ofstream f(root / "cwd/base/a.svh"); f << "wire from_cwd;\n"; }

    CwdGuard guard;
    std::filesystem::current_path(root / "cwd");
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites, dirs_] =
        pp.process("`include \"base/a.svh\"\n", (root / "lib/top.sv").string());
    REQUIRE(errs.empty());
    CHECK(out.find("wire from_lib;") != std::string::npos);
    CHECK(out.find("wire from_cwd;") == std::string::npos);
    REQUIRE(sites.size() == 1);
    CHECK(sites[0].file == (root / "lib/base/a.svh").string());
}

TEST_CASE("a relative include found only in the working directory is not found",
          "[compiler][preprocessor]") {
    const auto root = freshIncludeDir("svlsp_test_inc_cwd_only");
    std::filesystem::create_directories(root / "lib");
    { std::ofstream f(root / "only_in_cwd.svh"); f << "wire w;\n"; }

    CwdGuard guard;
    std::filesystem::current_path(root);
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites, dirs_] =
        pp.process("`include \"only_in_cwd.svh\"\n", (root / "lib/top.sv").string());
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message.find("cannot find \"only_in_cwd.svh\"") != std::string::npos);
    CHECK(sites.empty());
}

TEST_CASE("an include found through an include dir is named by its full, normalized path",
          "[compiler][preprocessor]") {
    const auto root = freshIncludeDir("svlsp_test_inc_normal");
    std::filesystem::create_directories(root / "src/base");
    std::filesystem::create_directories(root / "src/macros");
    { std::ofstream f(root / "src/base/a.svh"); f << "`include \"../macros/m.svh\"\n"; }
    { std::ofstream f(root / "src/macros/m.svh"); f << "wire from_m;\n"; }

    CwdGuard guard;
    std::filesystem::current_path(root / "src");
    SvPreprocessor pp({(root / "src/.").string()});
    auto [out, errs, macros_, map_, sites, dirs_] =
        pp.process("`include \"base/a.svh\"\n", (root / "tb/top.sv").string());
    REQUIRE(errs.empty());
    CHECK(out.find("wire from_m;") != std::string::npos);
    REQUIRE(sites.size() == 2);
    CHECK(sites[0].file == (root / "src/base/a.svh").string());
    CHECK(sites[1].file == (root / "src/macros/m.svh").string());
    CHECK(sites[1].includer == (root / "src/base/a.svh").string());
}

// ---------------------------------------------------------------------------
// Include errors (plan.md §6.35): every kind the preprocessor reports, where
// (the directive's file, line and range: P1), and that the rest of the unit
// is still processed.
// ---------------------------------------------------------------------------

namespace {
using Kind = PreprocessorError::Kind;

// The messages of the errors that are include errors, in order.
std::vector<std::string> includeErrors(const std::vector<PreprocessorError>& errs) {
    std::vector<std::string> out;
    for (const auto& e : errs)
        if (e.isInclude()) out.push_back(e.message);
    return out;
}
void writeFile(const std::filesystem::path& p, const std::string& text) {
    std::ofstream f(p);
    f << text;
}
// `e` is a `kind` error on `line` of `file`, over `directive` (the
// directive's text as written, from its backtick) starting at `column`.
void checkAt(const PreprocessorError& e, Kind kind, const std::string& file, int line,
             int column, const std::string& directive) {
    CHECK(e.kind == kind);
    CHECK(e.file == file);
    CHECK(e.line == line);
    CHECK(e.column == column);
    CHECK(e.endColumn == column + static_cast<int>(directive.size()));
}
}  // namespace

TEST_CASE("include error: a missing file is reported by name at its directive, and the unit goes on",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_missing");
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites, dirs] = pp.process(
        "wire before;\n"
        "`include \"incerr_missing.svh\"\n"
        "wire after;\n", (root / "top.sv").string());
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: cannot find \"incerr_missing.svh\"");
    checkAt(errs[0], Kind::IncludeNotFound, "", 2, 0, "`include \"incerr_missing.svh\"");
    CHECK(out.find("wire before;") != std::string::npos);
    CHECK(out.find("wire after;") != std::string::npos);
    CHECK(sites.empty());
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].resolved.empty());
}

TEST_CASE("include error: the range covers the directive, not its indent or a trailing comment",
          "[compiler][preprocessor][include-errors]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "module m;\n"
        "  `include <incerr_angle.svh>   // the reason\n"
        "endmodule\n", "/src/top.sv");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: cannot find \"incerr_angle.svh\"");
    checkAt(errs[0], Kind::IncludeNotFound, "", 2, 2, "`include <incerr_angle.svh>");
}

TEST_CASE("include error: a missing file named inside an included file is on that file",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_nested");
    const auto outer = (root / "outer.svh").string();
    writeFile(root / "outer.svh",
              "wire in_outer;\n"
              "`include \"incerr_deeper_missing.svh\"\n"
              "wire in_outer_after;\n");
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites, dirs] =
        pp.process("`include \"outer.svh\"\nwire top_after;\n", (root / "top.sv").string());
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: cannot find \"incerr_deeper_missing.svh\"");
    checkAt(errs[0], Kind::IncludeNotFound, outer, 2, 0, "`include \"incerr_deeper_missing.svh\"");
    CHECK(out.find("wire in_outer_after;") != std::string::npos);
    CHECK(out.find("wire top_after;") != std::string::npos);
    REQUIRE(sites.size() == 1);
    CHECK(sites[0].file == outer);
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[1].name == "incerr_deeper_missing.svh");
    CHECK(dirs[1].fromFile == outer);
    CHECK(dirs[1].resolved.empty());
}

TEST_CASE("include error: a file that includes itself is a cycle",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_self");
    const auto self = (root / "self.svh").string();
    writeFile(root / "self.svh", "wire in_self;\n`include \"self.svh\"\n");
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites_, dirs_] =
        pp.process("`include \"self.svh\"\nwire top_after;\n", (root / "top.sv").string());
    // Unguarded: the second visit reaches the directive again, and the
    // third entry is refused -- at self.svh's own directive.
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: cycle detected for \"" + self + "\"");
    checkAt(errs[0], Kind::IncludeCycle, self, 2, 0, "`include \"self.svh\"");
    CHECK(out.find("wire top_after;") != std::string::npos);
}

TEST_CASE("include error: two files that include each other are a cycle",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_pair");
    const auto a = (root / "a.svh").string();
    const auto b = (root / "b.svh").string();
    writeFile(a, "wire in_a;\n`include \"b.svh\"\n");
    writeFile(b, "wire in_b;\n`include \"a.svh\"\n");
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites, dirs_] =
        pp.process("`include \"a.svh\"\n", (root / "top.sv").string());
    // Reported once, at b.svh's directive on b's second visit.
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: cycle detected for \"" + a + "\"");
    checkAt(errs[0], Kind::IncludeCycle, b, 2, 0, "`include \"a.svh\"");
    CHECK(out.find("wire in_a;") != std::string::npos);
    CHECK(out.find("wire in_b;") != std::string::npos);
    CHECK(sites.size() == 2);
}

TEST_CASE("include error: including the same file twice, one after the other, is not a cycle",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_twice");
    writeFile(root / "g.svh",
              "`ifndef INCERR_G_SVH\n`define INCERR_G_SVH\nwire in_g;\n`endif\n");
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites, dirs_] =
        pp.process("`include \"g.svh\"\n`include \"g.svh\"\n", (root / "top.sv").string());
    CHECK(errs.empty());
    CHECK(out.find("wire in_g;") != std::string::npos);
    CHECK(sites.size() == 1);
}

// Guarded headers that include each other are fine in a simulator: the
// second visit's `ifndef is false, so the inner `include is never reached
// again. A file may be on the include stack twice; only a third entry (the
// second visit reached the same `include, so nothing guards it) is a cycle.
TEST_CASE("include error: guarded headers that include each other are not a cycle",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_guarded_pair");
    writeFile(root / "a.svh",
              "`ifndef INCERR_A_SVH\n`define INCERR_A_SVH\n"
              "`include \"b.svh\"\nwire in_a;\n`endif\n");
    writeFile(root / "b.svh",
              "`ifndef INCERR_B_SVH\n`define INCERR_B_SVH\n"
              "`include \"a.svh\"\nwire in_b;\n`endif\n");
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites, dirs_] =
        pp.process("`include \"a.svh\"\n", (root / "top.sv").string());
    CHECK(errs.empty());
    CHECK(out.find("wire in_a;") != std::string::npos);
    CHECK(out.find("wire in_b;") != std::string::npos);
    CHECK(sites.size() == 2);
}

TEST_CASE("include error: nesting deeper than the limit",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_depth");
    // A chain of distinct files, so it is never a cycle: d0 -> d1 -> ...
    constexpr int kChain = 20;
    for (int k = 0; k < kChain; ++k)
        writeFile(root / ("d" + std::to_string(k) + ".svh"),
                  "wire in_d" + std::to_string(k) + ";\n" +
                  (k + 1 < kChain ? "`include \"d" + std::to_string(k + 1) + ".svh\"\n" : ""));
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites, dirs_] =
        pp.process("`include \"d0.svh\"\nwire top_after;\n", (root / "top.sv").string());
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: depth limit exceeded (cycle?)");
    // 17 levels are read (d0..d16); d16's directive is the one refused.
    checkAt(errs[0], Kind::IncludeDepth, (root / "d16.svh").string(), 2, 0,
            "`include \"d17.svh\"");
    CHECK(sites.size() == 17);
    CHECK(out.find("wire in_d16;") != std::string::npos);
    CHECK(out.find("wire in_d17;") == std::string::npos);
    CHECK(out.find("wire top_after;") != std::string::npos);
}

TEST_CASE("include error: a file that exists but cannot be read",
          "[compiler][preprocessor][include-errors]") {
    if (geteuid() == 0) SKIP("root reads a mode-000 file");
    const auto root = freshIncludeDir("svlsp_test_incerr_unreadable");
    const auto locked = root / "locked.svh";
    writeFile(locked, "wire in_locked;\n");
    std::filesystem::permissions(locked, std::filesystem::perms::none);
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites, dirs] =
        pp.process("`include \"locked.svh\"\nwire top_after;\n", (root / "top.sv").string());
    std::filesystem::permissions(locked, std::filesystem::perms::owner_all);
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message == "`include: cannot open \"" + locked.string() + "\"");
    checkAt(errs[0], Kind::IncludeUnreadable, "", 1, 0, "`include \"locked.svh\"");
    CHECK(out.find("wire in_locked;") == std::string::npos);
    CHECK(out.find("wire top_after;") != std::string::npos);
    CHECK(sites.empty());
    // It resolved -- it is not a name-not-found.
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].resolved == locked.string());
}

TEST_CASE("include error: malformed directives",
          "[compiler][preprocessor][include-errors]") {
    // {source, line of the directive, the directive's extent}. `include
    // `MACRO is §6.36; until then it is malformed too.
    struct Case { const char* src; int line; const char* directive; };
    const Case cases[] = {
        {"`include\n", 1, "`include"},
        {"`include \"unterminated.svh\n", 1, "`include \"unterminated.svh"},
        {"`include <unterminated.svh\n", 1, "`include <unterminated.svh"},
        {"`include bare_name.svh // c\n", 1, "`include bare_name.svh"},
        {"`include \"\"\n", 1, "`include \"\""},
        {"`define INCERR_FILE \"x.svh\"\n`include `INCERR_FILE\n", 2, "`include `INCERR_FILE"},
    };
    for (const auto& c : cases) {
        INFO(c.src);
        SvPreprocessor pp;
        auto [out, errs, macros_, map_, sites, dirs] =
            pp.process(std::string(c.src) + "wire after;\n", "/src/top.sv");
        REQUIRE(errs.size() == 1);
        CHECK(errs[0].message == "`include: missing or malformed filename");
        checkAt(errs[0], Kind::IncludeMalformed, "", c.line, 0, c.directive);
        CHECK(out.find("wire after;") != std::string::npos);
        CHECK(sites.empty());
        CHECK(dirs.empty());
    }
}

TEST_CASE("include error: every failing directive in a unit is reported, in order",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_many");
    writeFile(root / "ok.svh", "wire in_ok;\n");
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites, dirs_] = pp.process(
        "`include \"incerr_first.svh\"\n"
        "`include \"ok.svh\"\n"
        "`include bad\n"
        "`include \"incerr_second.svh\"\n", (root / "top.sv").string());
    CHECK(includeErrors(errs) == std::vector<std::string>{
        "`include: cannot find \"incerr_first.svh\"",
        "`include: missing or malformed filename",
        "`include: cannot find \"incerr_second.svh\""});
    REQUIRE(errs.size() == 3);
    CHECK(errs[0].line == 1);
    CHECK(errs[1].line == 3);
    CHECK(errs[2].line == 4);
    CHECK(out.find("wire in_ok;") != std::string::npos);
    CHECK(sites.size() == 1);
}

// The other preprocessor errors are not shown yet (§6.35 I3 / P4), but
// carry the line they were found on already: kind Other, the whole line.
TEST_CASE("preprocessor errors other than include errors carry their line",
          "[compiler][preprocessor][include-errors]") {
    const auto root = freshIncludeDir("svlsp_test_incerr_other");
    const auto inc = (root / "inc.svh").string();
    writeFile(inc, "wire w;\nwire x = `INCERR_UNDEFINED;\n");
    SvPreprocessor pp({root.string()});
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`include \"inc.svh\"\n"
        "  `endif\n"
        "`ifdef INCERR_NOT_DEFINED\n"
        "wire y;\n", (root / "top.sv").string());
    REQUIRE(errs.size() == 3);
    CHECK(errs[0].message == "undefined macro `INCERR_UNDEFINED`");
    CHECK(errs[0].kind == Kind::MacroUndefined);
    CHECK(errs[0].file == inc);
    CHECK(errs[0].line == 2);
    CHECK(errs[0].column == 0);
    CHECK(errs[0].endColumn == static_cast<int>(std::string("wire x = `INCERR_UNDEFINED;").size()));
    CHECK(errs[1].message == "`endif without `ifdef");
    CHECK(errs[1].kind == Kind::Other);
    CHECK(errs[1].file.empty());
    CHECK(errs[1].line == 2);
    CHECK(errs[1].column == 2);  // the directive, past its indent
    CHECK(errs[1].endColumn == 8);
    // An unterminated block points at its opening directive.
    CHECK(errs[2].message == "unterminated `ifdef/`ifndef block");
    CHECK(errs[2].file.empty());
    CHECK(errs[2].line == 3);
    CHECK(includeErrors(errs).empty());
}

TEST_CASE("include: __LINE__ inside included file resolves to its own line number",
          "[compiler][preprocessor]") {
    // Pass 1 (CompilerDirectiveStripper) only ever runs on the top-level
    // source before SvPreprocessor::process is called; included files are
    // read as raw text by processInclude. __LINE__ must still resolve
    // against the included file's own line numbers, not be left as a
    // literal, undefined macro invocation.
    std::string tmpPath = "/tmp/svlsp_test_inc_line.sv";
    { std::ofstream f(tmpPath); f << "wire a;\n"
                                     "wire b = `__LINE__;\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire b = 2;") != std::string::npos);
}

TEST_CASE("include: __FILE__ inside included file resolves to the included path",
          "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_file.sv";
    { std::ofstream f(tmpPath); f << "string s = `__FILE__;\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("string s = \"" + tmpPath + "\";") != std::string::npos);
}

TEST_CASE("include: metadata directive inside included file is stripped without error",
          "[compiler][preprocessor]") {
    // `timescale etc. are pass-1 directives; an included file containing one
    // must not fall through to pass 2 and be treated as an unknown directive.
    std::string tmpPath = "/tmp/svlsp_test_inc_timescale.sv";
    { std::ofstream f(tmpPath); f << "`timescale 1ns/1ps\n"
                                     "wire c;\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire c;") != std::string::npos);
    REQUIRE(out.find("timescale") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Real-time `include` progress logging (svlsp --build-db's progress counter)
// ---------------------------------------------------------------------------

TEST_CASE("include resolution logs a progress line as soon as the file is opened",
          "[compiler][preprocessor]") {
    // The log line must appear the moment `include` is resolved -- not only
    // once the whole top-level process() call returns -- so a caller like
    // CompilationController's --build-db progress counter gets real
    // incremental feedback even for a single top-level file that `include`s
    // an entire library (the motivating real-world case: one uvm_pkg.sv
    // `include`ing ~140 files, where a post-hoc "log after the whole parse
    // finishes" approach gives no signal at all until the very end).
    std::string tmpPath = "/tmp/svlsp_test_inc_progress_a.sv";
    { std::ofstream f(tmpPath); f << "wire w;\n"; }

    std::ostringstream progress;
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] =
        pp.process("`include \"" + tmpPath + "\"\n", "test.sv", &progress);
    REQUIRE(errs.empty());
    CHECK(progress.str() == "[parsed]   included: " + tmpPath + "\n");
}

TEST_CASE("nested includes each log their own progress line, in encounter order",
          "[compiler][preprocessor]") {
    std::string innerPath = "/tmp/svlsp_test_inc_progress_inner.sv";
    std::string midPath   = "/tmp/svlsp_test_inc_progress_mid.sv";
    { std::ofstream f(innerPath); f << "wire inner;\n"; }
    { std::ofstream f(midPath); f << "`include \"" + innerPath + "\"\nwire mid;\n"; }

    std::ostringstream progress;
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] =
        pp.process("`include \"" + midPath + "\"\n", "test.sv", &progress);
    REQUIRE(errs.empty());
    // mid.sv is opened (and logged) before its own nested `include` of
    // inner.sv is reached, so mid's line comes first.
    CHECK(progress.str() ==
          "[parsed]   included: " + midPath + "\n"
          "[parsed]   included: " + innerPath + "\n");
}

TEST_CASE("a missing include file logs no progress line", "[compiler][preprocessor]") {
    std::ostringstream progress;
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] =
        pp.process("`include \"no_such_file.sv\"\n", "test.sv", &progress);
    REQUIRE(!errs.empty());
    CHECK(progress.str().empty());
}

TEST_CASE("progressLog defaults to null and is a no-op when omitted",
          "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_progress_noop.sv";
    { std::ofstream f(tmpPath); f << "wire w;\n"; }

    SvPreprocessor pp;
    // No progressLog argument at all -- must behave exactly like every
    // pre-existing `include` test above, not crash on a null stream.
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire w;") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Line preservation
// ---------------------------------------------------------------------------

TEST_CASE("directive lines become blank lines preserving line count", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_, sites_, dirs_] = pp.process(
        "`define A 1\n"  // line 1 → blank
        "wire a;\n"      // line 2 → kept
        "`define B 2\n"  // line 3 → blank
        "wire b;\n",     // line 4 → kept
        "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out == "\nwire a;\n\nwire b;\n");
}

// ---------------------------------------------------------------------------
// Macro records (Phase 4.4)
// ---------------------------------------------------------------------------

TEST_CASE("macro record captured for object-like define", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process("`define WIDTH 8\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].name == "WIDTH");
    CHECK(result.macros[0].body == "8");
    CHECK(result.macros[0].line == 1);
}

TEST_CASE("macro record body excludes trailing line comment", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process("`define BUS_W 16 // bus width\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].body == "16");
}

TEST_CASE("multiple macro records captured in order", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define FOO 1\n"
        "wire a;\n"
        "`define BAR 2\n",
        "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 2);
    CHECK(result.macros[0].name == "FOO");
    CHECK(result.macros[0].line == 1);
    CHECK(result.macros[1].name == "BAR");
    CHECK(result.macros[1].line == 3);
}

TEST_CASE("macro in inactive ifdef branch not captured", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`ifdef NEVER_DEFINED\n"
        "`define HIDDEN 42\n"
        "`endif\n",
        "f.sv");
    REQUIRE(result.errors.empty());
    CHECK(result.macros.empty());
}

TEST_CASE("macro in active ifdef branch captured", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    pp.define("SIM");
    auto result = pp.process(
        "`ifdef SIM\n"
        "`define CLK_PERIOD 10\n"
        "`endif\n",
        "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].name == "CLK_PERIOD");
}

TEST_CASE("function-like macro record captured", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process("`define MAX(a, b) ((a) > (b) ? (a) : (b))\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].name == "MAX");
    CHECK(!result.macros[0].body.empty());
}

// ---------------------------------------------------------------------------
// Multi-line (backslash-continuation) `define bodies
// ---------------------------------------------------------------------------

TEST_CASE("backslash-continuation define merges body across two lines",
          "[compiler][preprocessor][multiline]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define WIDE_WIDTH \\\n"
        "    16\n"
        "wire [`WIDE_WIDTH-1:0] bus;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].name == "WIDE_WIDTH");
    CHECK(result.macros[0].body == "16");
    CHECK(result.macros[0].line == 1);
    CHECK(result.source.find("wire [16-1:0] bus;") != std::string::npos);
}

TEST_CASE("backslash-continuation define merges body across three lines",
          "[compiler][preprocessor][multiline]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define LONG_MACRO a \\\n"
        "b \\\n"
        "c\n"
        "wire w = `LONG_MACRO;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].body == "a b c");
    CHECK(result.source.find("wire w = a b c;") != std::string::npos);
}

TEST_CASE("backslash-continuation define preserves output line count",
          "[compiler][preprocessor][multiline]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define WIDE_WIDTH \\\n"  // line 1 -> blank
        "    16\n"                // line 2 -> blank
        "\n"                      // line 3 -> blank
        "wire [`WIDE_WIDTH-1:0] bus;\n" // line 4 -> content
        "wire done;\n",           // line 5 -> content
        "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 5);
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].line == 2);
    CHECK(result.sourceMap[2].line == 3);
    CHECK(result.sourceMap[3].line == 4);
    CHECK(result.sourceMap[4].line == 5);
    int newlines = static_cast<int>(std::count(result.source.begin(), result.source.end(), '\n'));
    CHECK(newlines == 5);
}

TEST_CASE("backslash-continuation define followed by mid-line invocation records correct column shift",
          "[compiler][preprocessor][multiline]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define WIDE_WIDTH \\\n"
        "    16\n"
        "wire [`WIDE_WIDTH-1:0] wide_bus;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 3);
    // "`WIDE_WIDTH" (11 chars) expands to "16" (2 chars) -> delta of +9.
    REQUIRE(result.sourceMap[2].colShifts.size() == 1);
    CHECK(result.sourceMap[2].colShifts[0].delta == 9);
}

// ---------------------------------------------------------------------------
// Multi-line macro invocations (no backslash — real UVM pattern, e.g.
// `UVM_PH_TRACE(ID, MSG, PH, VERB) with the argument list split across
// physical lines purely via an open '(')
// ---------------------------------------------------------------------------

TEST_CASE("function-like macro invocation with args spanning two lines expands",
          "[compiler][preprocessor][multiinvoke]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define M(A, B, C) A B C\n"
        "`M(\"x\",\n"
        "y, z)\n", "f.sv");
    REQUIRE(result.errors.empty());
    CHECK(result.source.find("\"x\" y z") != std::string::npos);
}

TEST_CASE("function-like macro invocation with args spanning three lines expands",
          "[compiler][preprocessor][multiinvoke]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define M(A, B, C) A B C\n"
        "`M(\"x\",\n"
        "y,\n"
        "z)\n", "f.sv");
    REQUIRE(result.errors.empty());
    CHECK(result.source.find("\"x\" y z") != std::string::npos);
}

TEST_CASE("function-like macro invocation split across lines mid-statement expands",
          "[compiler][preprocessor][multiinvoke]") {
    // Mirrors real UVM: the invocation doesn't start the line.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define M(A, B) A B\n"
        "if (x) `M(\"id\",\n"
        "second);\n", "f.sv");
    REQUIRE(result.errors.empty());
    CHECK(result.source.find("if (x) \"id\" second;") != std::string::npos);
}

TEST_CASE("function-like macro invocation spanning lines preserves output line count",
          "[compiler][preprocessor][multiinvoke]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define M(A, B, C) A B C\n"   // line 1 -> blank
        "`M(\"x\",\n"                  // line 2 -> real content
        "y,\n"                         // line 3 -> blank
        "z)\n"                         // line 4 -> blank
        "wire done;\n",                // line 5 -> content
        "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 5);
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].line == 2);
    CHECK(result.sourceMap[2].line == 3);
    CHECK(result.sourceMap[3].line == 4);
    CHECK(result.sourceMap[4].line == 5);
    int newlines = static_cast<int>(std::count(result.source.begin(), result.source.end(), '\n'));
    CHECK(newlines == 5);
}

TEST_CASE("unknown macro name does not trigger multi-line merging",
          "[compiler][preprocessor][multiinvoke]") {
    // An undefined (or object-like) name followed by an unrelated, unbalanced
    // '(' elsewhere in ordinary code must not cause line-swallowing.
    SvPreprocessor pp;
    auto result = pp.process(
        "wire w = foo(a,\n"
        "b);\n", "f.sv");
    REQUIRE(result.sourceMap.size() == 2);
}

// ---------------------------------------------------------------------------
// Source map
// ---------------------------------------------------------------------------

TEST_CASE("source map has one entry per output line for plain source",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "wire a;\n"
        "wire b;\n"
        "wire c;\n", "main.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 3);
    // Primary file lines use "" (caller already knows the file path).
    CHECK(result.sourceMap[0].file == "");
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].file == "");
    CHECK(result.sourceMap[1].line == 2);
    CHECK(result.sourceMap[2].file == "");
    CHECK(result.sourceMap[2].line == 3);
}

TEST_CASE("source map directive lines map to their original line",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define A 1\n"   // line 1 → blank output line
        "wire a;\n"       // line 2
        "`define B 2\n",  // line 3 → blank output line
        "main.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 3);
    CHECK(result.sourceMap[0].file == "");
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].file == "");
    CHECK(result.sourceMap[1].line == 2);
    CHECK(result.sourceMap[2].file == "");
    CHECK(result.sourceMap[2].line == 3);
}

TEST_CASE("source map include lines point to included file",
          "[compiler][preprocessor][sourcemap]") {
    std::string tmpPath = "/tmp/svlsp_test_sm_inc.sv";
    { std::ofstream f(tmpPath); f << "wire x;\n"; }

    SvPreprocessor pp;
    auto result = pp.process(
        "wire a;\n"
        "`include \"" + tmpPath + "\"\n"
        "wire b;\n",
        "main.sv");
    REQUIRE(result.errors.empty());
    // 3 output lines: wire a (primary:""), wire x (inc:tmpPath), wire b (primary:"")
    REQUIRE(result.sourceMap.size() == 3);
    CHECK(result.sourceMap[0].file == "");      // primary file
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].file == tmpPath); // included file
    CHECK(result.sourceMap[1].line == 1);
    CHECK(result.sourceMap[2].file == "");      // primary file
    CHECK(result.sourceMap[2].line == 3);
}

TEST_CASE("line without macro invocation has no column shifts",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process("wire a;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 1);
    CHECK(result.sourceMap[0].colShifts.empty());
}

TEST_CASE("mid-line shrinking macro invocation records a column shift breakpoint",
          "[compiler][preprocessor][sourcemap]") {
    // "`WIDTH" (6 chars) expands to "8" (1 char) -> delta of +5 for columns
    // at/after the point right after the replacement.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define WIDTH 8\n"
        "wire [`WIDTH-1:0] data_bus;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 2);
    REQUIRE(result.sourceMap[1].colShifts.size() == 1);
    CHECK(result.sourceMap[1].colShifts[0].outputCol == 7);
    CHECK(result.sourceMap[1].colShifts[0].delta == 5);
}

TEST_CASE("mid-line growing macro invocation records a negative column shift",
          "[compiler][preprocessor][sourcemap]") {
    // "`FOO" (4 chars) expands to "abcdefgh" (8 chars) -> delta of -4.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define FOO abcdefgh\n"
        "wire `FOO x;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 2);
    REQUIRE(result.sourceMap[1].colShifts.size() == 1);
    CHECK(result.sourceMap[1].colShifts[0].outputCol == 13);
    CHECK(result.sourceMap[1].colShifts[0].delta == -4);
}

TEST_CASE("multiple macros on one line accumulate column shift delta",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define A 1\n"
        "`define B 22\n"
        "wire `A `B x;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 3);
    REQUIRE(result.sourceMap[2].colShifts.size() == 2);
    CHECK(result.sourceMap[2].colShifts[0].outputCol == 6);
    CHECK(result.sourceMap[2].colShifts[0].delta == 1);
    CHECK(result.sourceMap[2].colShifts[1].outputCol == 9);
    CHECK(result.sourceMap[2].colShifts[1].delta == 1);
}

TEST_CASE("source map entry count equals output line count",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define W 8\n"
        "`ifdef SIM\n"
        "wire sim_only;\n"
        "`endif\n"
        "wire w;\n",
        "f.sv");
    // Count '\n' in output
    int newlines = static_cast<int>(std::count(result.source.begin(), result.source.end(), '\n'));
    CHECK(static_cast<int>(result.sourceMap.size()) == newlines);
}

// ---------------------------------------------------------------------------
// Stringification (`"..`") -- Gap C (partial: stringification only, not
// token-pasting)
// ---------------------------------------------------------------------------

TEST_CASE("stringification wraps a substituted macro parameter in quotes",
          "[compiler][preprocessor][stringify]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define STR(T) `\"T`\"\n"
        "string s = `STR(foo);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\nstring s = \"foo\";\n");
}

TEST_CASE("stringification reproduces the real UVM `uvm_type_name_decl pattern",
          "[compiler][preprocessor][stringify]") {
    // macros/uvm_object_defines.svh:540 (`uvm_type_name_decl) and :555
    // (`m_uvm_object_registry_internal, which invokes it as
    // `uvm_type_name_decl(`"S`") -- session 3's root-caused Gap C site.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define uvm_type_name_decl(TNAME_STRING) \\\n"
        "  const static function string type_name(); return `\"TNAME_STRING`\"; endfunction\n"
        "`uvm_type_name_decl(uvm_reg_err_service)\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source.find("return \"uvm_reg_err_service\";") != std::string::npos);
}

TEST_CASE("stringification macro-expands its contents before quoting",
          "[compiler][preprocessor][stringify]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define A hello\n"
        "`define STR() `\"`A`\"\n"
        "string s = `STR();\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\n\nstring s = \"hello\";\n");
}

TEST_CASE("unterminated stringification records an error instead of hanging",
          "[compiler][preprocessor][stringify]") {
    SvPreprocessor pp;
    auto result = pp.process("string s = `\"unterminated;\n", "f.sv");
    REQUIRE_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// Token-pasting (``)
// ---------------------------------------------------------------------------

TEST_CASE("token-pasting splices two identifier fragments",
          "[compiler][preprocessor][tokenpaste]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define CONCAT(A,B) A``B\n"
        "wire w = `CONCAT(foo,bar);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\nwire w = foobar;\n");
}

TEST_CASE("token-pasting splices onto a non-identifier right operand",
          "[compiler][preprocessor][tokenpaste]") {
    // Mirrors real UVM's `M__TABLE_Q(QUEUE_NAME) QUEUE_NAME``.value
    // (base/uvm_resource_pool.svh:136) -- the paste is a pure textual
    // splice, not identifier-fragment-only.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define FIELD(NAME) NAME``.value\n"
        "x = `FIELD(rq);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\nx = rq.value;\n");
}

TEST_CASE("token-pasting splices three fragments around a parameter",
          "[compiler][preprocessor][tokenpaste]") {
    // Mirrors real UVM's base/uvm_packer.svh:449
    // `` function void uvm_packer::get_packed_``T``s (...) ``
    SvPreprocessor pp;
    auto result = pp.process(
        "`define GETTER(T) get_packed_``T``s\n"
        "function void `GETTER(bit) (int x); endfunction\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source.find("function void get_packed_bits (int x); endfunction") !=
            std::string::npos);
}

TEST_CASE("token-pasting a macro name then invoking it",
          "[compiler][preprocessor][tokenpaste]") {
    // Mirrors real UVM's macros/uvm_object_defines.svh:1456
    // `` `uvm_pack_``TYPE``(ARG, __local_packer__) `` -- proves the splice
    // happens before expandStr scans for the invocation's macro name.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define uvm_pack_int(X) pack_int_impl(X)\n"
        "`define DISPATCH(TYPE, ARG) `uvm_pack_``TYPE``(ARG)\n"
        "`DISPATCH(int, myvar);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source.find("pack_int_impl(myvar);") != std::string::npos);
}

TEST_CASE("token-pasting resolves before a later stringification span sees it",
          "[compiler][preprocessor][tokenpaste]") {
    // Mirrors real UVM's macros/uvm_phase_defines.svh:58
    // `` `uvm_type_name_decl(`"PREFIX``PHASE``_phase`") ``
    SvPreprocessor pp;
    auto result = pp.process(
        "`define STR2(PREFIX, PHASE) `\"PREFIX``PHASE``_phase`\"\n"
        "string s = `STR2(my, build);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\nstring s = \"mybuild_phase\";\n");
}

TEST_CASE("token-pasting reproduces the real UVM `uvm_register_cb pattern",
          "[compiler][preprocessor][tokenpaste]") {
    // macros/uvm_callback_defines.svh:71-72 -- combines token-pasting and
    // stringification in the same macro body; also the exact site behind
    // session 3's symbol-table-pollution bug (uvm_report_catcher.svh:71).
    SvPreprocessor pp;
    auto result = pp.process(
        "`define uvm_register_cb(T,CB) \\\n"
        "  static local bit m_register_cb_``CB = uvm_callbacks#(T,CB)::m_register_pair(`\"T`\",`\"CB`\");\n"
        "`uvm_register_cb(uvm_report_object,uvm_report_catcher)\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source.find(
        "static local bit m_register_cb_uvm_report_catcher = "
        "uvm_callbacks#(uvm_report_object,uvm_report_catcher)::m_register_pair("
        "\"uvm_report_object\",\"uvm_report_catcher\");") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Macro records carry parameters, defaults, column and file (plan.md §6.29
// part A)
// ---------------------------------------------------------------------------

TEST_CASE("macro record keeps a function-like macro's parameters and defaults",
          "[compiler][preprocessor][phase6.29]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define PLAIN 8\n"
        "  `define M(A, B=1, RO=get_obj(x, y)) A+B\n"
        "`define Z() 0\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 3);

    const auto& plain = result.macros[0];
    CHECK_FALSE(plain.isFunctionLike);
    CHECK(plain.params.empty());
    CHECK(plain.column == 8);
    CHECK(plain.file.empty()); // primary file

    const auto& m = result.macros[1];
    CHECK(m.name == "M");
    CHECK(m.isFunctionLike);
    CHECK(m.line == 2);
    CHECK(m.column == 10);
    REQUIRE(m.params == std::vector<std::string>{"A", "B", "RO"});
    REQUIRE(m.defaults.size() == 3);
    CHECK_FALSE(m.defaults[0].has_value());
    CHECK(m.defaults[1] == std::optional<std::string>{"1"});
    CHECK(m.defaults[2] == std::optional<std::string>{"get_obj(x, y)"});

    const auto& z = result.macros[2];
    CHECK(z.isFunctionLike);
    CHECK(z.params.empty());
}

TEST_CASE("macro record from an included file names that file",
          "[compiler][preprocessor][phase6.29]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_p29.svh";
    { std::ofstream f(tmpPath); f << "// header\n`define INC_M(X) X\n"; }

    SvPreprocessor pp;
    auto result = pp.process("`include \"" + tmpPath + "\"\n`define TOP_M 1\n", "test.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 2);
    CHECK(result.macros[0].name == "INC_M");
    CHECK(result.macros[0].file == tmpPath);
    CHECK(result.macros[0].line == 2);
    CHECK(result.macros[1].name == "TOP_M");
    CHECK(result.macros[1].file.empty());
}

// ---------------------------------------------------------------------------
// Conditionals inside a `define body (plan.md §6.35 P4): evaluated when the
// macro is expanded, against the macros defined at that point; only the
// active branch is kept. UVM's field macros rely on it.
// ---------------------------------------------------------------------------

namespace {
// The preprocessed text with runs of whitespace collapsed to one space and
// the ends trimmed, so a check doesn't depend on the blanks a directive
// leaves behind.
std::string squash(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n') { space = !out.empty(); continue; }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}
PreprocessorResult pp(const std::string& src, std::vector<std::string> defines = {}) {
    SvPreprocessor p;
    for (const auto& d : defines) p.define(d);
    return p.process(src, "/src/t.sv");
}
}  // namespace

TEST_CASE("define-body conditionals: `ifndef keeps its text when the macro is undefined",
          "[compiler][preprocessor][define-cond]") {
    const std::string src =
        "`define M(X) \\\n"
        "`ifndef DC_FOO \\\n"
        "  int X; \\\n"
        "`endif\n"
        "`M(a)\n";
    auto r = pp(src);
    CHECK(r.errors.empty());
    CHECK(squash(r.source) == "int a;");

    auto d = pp(src, {"DC_FOO"});
    CHECK(d.errors.empty());
    CHECK(squash(d.source).empty());
}

TEST_CASE("define-body conditionals: `ifdef / `else picks one branch",
          "[compiler][preprocessor][define-cond]") {
    const std::string src =
        "`define M `ifdef DC_A int on; `else int off; `endif\n"
        "`M\n";
    CHECK(squash(pp(src).source) == "int off;");
    CHECK(squash(pp(src, {"DC_A"}).source) == "int on;");
    CHECK(pp(src).errors.empty());
}

TEST_CASE("define-body conditionals: an `elsif chain takes the first true branch only",
          "[compiler][preprocessor][define-cond]") {
    const std::string src =
        "`define M `ifdef DC_A a `elsif DC_B b `elsif DC_C c `else d `endif\n"
        "`M\n";
    CHECK(squash(pp(src).source) == "d");
    CHECK(squash(pp(src, {"DC_B"}).source) == "b");
    CHECK(squash(pp(src, {"DC_B", "DC_C"}).source) == "b");
    CHECK(squash(pp(src, {"DC_A", "DC_C"}).source) == "a");
}

TEST_CASE("define-body conditionals: nested blocks",
          "[compiler][preprocessor][define-cond]") {
    const std::string src =
        "`define M `ifdef DC_A a1 `ifdef DC_B ab `else anb `endif a2 `else na `endif\n"
        "`M\n";
    CHECK(squash(pp(src).source) == "na");
    CHECK(squash(pp(src, {"DC_A"}).source) == "a1 anb a2");
    CHECK(squash(pp(src, {"DC_A", "DC_B"}).source) == "a1 ab a2");
    // An inactive outer branch skips an inner one that would be true.
    CHECK(squash(pp(src, {"DC_B"}).source) == "na");
}

TEST_CASE("define-body conditionals: evaluated at each use, not at the `define",
          "[compiler][preprocessor][define-cond]") {
    auto r = pp(
        "`define M `ifdef DC_LATE late `else early `endif\n"
        "`M\n"
        "`define DC_LATE\n"
        "`M\n"
        "`undef DC_LATE\n"
        "`M\n");
    CHECK(r.errors.empty());
    CHECK(squash(r.source) == "early late early");
}

TEST_CASE("define-body conditionals: a nested macro's own conditionals",
          "[compiler][preprocessor][define-cond]") {
    auto r = pp(
        "`define INNER `ifdef DC_I i_on `else i_off `endif\n"
        "`define OUTER(X) `ifndef DC_O X `INNER `endif\n"
        "`OUTER(o)\n",
        {"DC_I"});
    CHECK(r.errors.empty());
    CHECK(squash(r.source) == "o i_on");
}

TEST_CASE("define-body conditionals: a directive inside a string literal is text",
          "[compiler][preprocessor][define-cond]") {
    auto r = pp(
        "`define M s = \"`ifdef DC_X not a directive\"; `ifdef DC_X x `endif\n"
        "`M\n");
    CHECK(r.errors.empty());
    CHECK(squash(r.source) == "s = \"`ifdef DC_X not a directive\";");
}

// The three UVM macros (macros/uvm_object_defines.svh) P0 counted 19
// "undefined macro `ifndef`" errors from, in their shapes.
TEST_CASE("define-body conditionals: the UVM field-macro shapes expand without errors",
          "[compiler][preprocessor][define-cond]") {
    const std::string defs =
        // m_uvm_field_op_begin: token-pasting around the conditional.
        "`define DC_OP_BEGIN(OP, FLAG) \\\n"
        "UVM_``OP: \\\n"
        "  if ( \\\n"
        "     `ifndef DC_LEGACY (((FLAG)&UVM_``OP)) && `endif \\\n"
        "     (!((FLAG)&UVM_NO``OP)) \\\n"
        "  ) begin\n"
        // m_warn_if_no_positive_ops: strings in both branches, one naming
        // the macro the condition tests.
        "`define DC_WARN(ARG,FLAG) \\\n"
        "  begin \\\n"
        "      string behavior; \\\n"
        "      `ifdef DC_LEGACY behavior = \"As DC_LEGACY is set\"; \\\n"
        "      `else behavior = \"per IEEE (see Mantis 7187)\"; \\\n"
        "      `endif \\\n"
        "  end\n"
        // m_uvm_field_begin: a nested macro, then a stringified argument.
        "`define DC_FIELD_BEGIN(ARG, FLAG, REGEX=\"\") \\\n"
        "  `DC_WARN(ARG,FLAG) \\\n"
        "        if ( \\\n"
        "           `ifndef DC_LEGACY (((FLAG)&UVM_SET)) && `endif \\\n"
        "           (!((FLAG)&UVM_NOSET)) \\\n"
        "           ) begin \\\n"
        "          names.push_back('{`\"ARG`\", REGEX}); \\\n"
        "        end\n";
    const std::string uses =
        "`DC_OP_BEGIN(COPY, f)\n"
        "`DC_FIELD_BEGIN(fld, f)\n";

    auto r = pp(defs + uses);
    CHECK(r.errors.empty());
    const auto out = squash(r.source);
    CHECK(out.find("UVM_COPY: if ( (((f)&UVM_COPY)) && (!((f)&UVM_NOCOPY)) ) begin") !=
          std::string::npos);
    CHECK(out.find("behavior = \"per IEEE (see Mantis 7187)\";") != std::string::npos);
    CHECK(out.find("As DC_LEGACY is set") == std::string::npos);
    CHECK(out.find("if ( (((f)&UVM_SET)) && (!((f)&UVM_NOSET)) ) begin") != std::string::npos);
    CHECK(out.find("names.push_back('{\"fld\", \"\"});") != std::string::npos);
    CHECK(out.find('`') == std::string::npos);

    auto legacy = pp(defs + uses, {"DC_LEGACY"});
    CHECK(legacy.errors.empty());
    const auto lout = squash(legacy.source);
    CHECK(lout.find("UVM_COPY: if ( (!((f)&UVM_NOCOPY)) ) begin") != std::string::npos);
    CHECK(lout.find("behavior = \"As DC_LEGACY is set\";") != std::string::npos);
}

TEST_CASE("define-body conditionals: the expansion keeps the source map one line per line",
          "[compiler][preprocessor][define-cond]") {
    const std::string src =
        "`define M(X) \\\n"
        "`ifdef DC_A \\\n"
        "  int X; \\\n"
        "`endif\n"
        "module m;\n"
        "  `M(a) wire w;\n"
        "endmodule\n";
    auto r = pp(src, {"DC_A"});
    CHECK(r.errors.empty());
    REQUIRE(r.sourceMap.size() == 7);
    CHECK(r.sourceMap[5].line == 6);
    CHECK(squash(r.source) == "module m; int a; wire w; endmodule");
}

TEST_CASE("define-body conditionals: unbalanced directives in a body are errors",
          "[compiler][preprocessor][define-cond]") {
    auto stray = pp("`define M a `endif b\n`M\n");
    REQUIRE(stray.errors.size() == 1);
    CHECK(stray.errors[0].message == "`endif without `ifdef in the body of macro `M`");
    CHECK(stray.errors[0].line == 2);
    CHECK(squash(stray.source) == "a b");

    auto open = pp("`define M `ifdef DC_A a\n`M\n");
    REQUIRE(open.errors.size() == 1);
    CHECK(open.errors[0].message == "unterminated `ifdef/`ifndef in the body of macro `M`");
    CHECK(open.errors[0].line == 2);

    auto noName = pp("`define M `ifdef a `endif\n`M\n");
    CHECK(noName.errors.empty());  // "a" is the name tested
    auto missing = pp("`define M `ifdef `endif\n`M\n");
    REQUIRE(missing.errors.size() == 1);
    CHECK(missing.errors[0].message == "`ifdef without a macro name in the body of macro `M`");
}

TEST_CASE("a macro invocation inside a string literal is not expanded",
          "[compiler][preprocessor][define-cond]") {
    SvPreprocessor p;
    auto r = p.process(
        "`define DC_S 1\n"
        "string s = \"`DC_S and \\\"`DC_S\\\"\"; int i = `DC_S;\n", "/src/t.sv");
    CHECK(r.errors.empty());
    CHECK(r.source.find("string s = \"`DC_S and \\\"`DC_S\\\"\"; int i = 1;") != std::string::npos);
}

TEST_CASE("an error found while expanding a line covers the line past its indent",
          "[compiler][preprocessor][include-errors]") {
    SvPreprocessor p;
    auto r = p.process("module m;\n    wire w = `PP_NOPE + 1;   // c\nendmodule\n", "/src/t.sv");
    REQUIRE(r.errors.size() == 1);
    CHECK(r.errors[0].kind == PreprocessorError::Kind::MacroUndefined);
    CHECK(r.errors[0].line == 2);
    CHECK(r.errors[0].column == 4);
    CHECK(r.errors[0].endColumn == static_cast<int>(std::string("    wire w = `PP_NOPE + 1;").size()));
    CHECK_FALSE(r.errors[0].isInclude());
}

TEST_CASE("a conditional directive's error covers the directive, indented and commented",
          "[compiler][preprocessor][include-errors]") {
    SvPreprocessor p;
    auto r = p.process("module m;\n    `else   // stray\nendmodule\n", "/src/t.sv");
    REQUIRE(r.errors.size() == 1);
    CHECK(r.errors[0].message == "`else without `ifdef");
    CHECK(r.errors[0].column == 4);
    CHECK(r.errors[0].endColumn == 9);
}
