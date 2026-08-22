#include "uvm_corpus_fixture.h"
#include "db/database.h"
#include "db/compilation_controller.h"
#include "compiler/project_config.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

std::string uvmCorpusRoot() {
    if (const char* env = std::getenv("SVLSP_UVM_CORPUS_DIR")) {
        return env;
    }
    return "/home/martin/src/verilator_test/uvm-core/src";
}

static std::string readFileOrThrow(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(
            "uvm_corpus_tests: cannot open '" + path.string() + "'. "
            "This binary needs a real UVM-core checkout (see handoff.md's "
            "\"UVM real-world smoke test\" sections for setup). Point "
            "SVLSP_UVM_CORPUS_DIR at its src/ directory, or check out UVM at "
            "the default path this whole line of work has used: "
            "/home/martin/src/verilator_test/uvm-core/src");
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string readCorpusFile(const std::string& relPath) {
    return readFileOrThrow(fs::path(uvmCorpusRoot()) / relPath);
}

lsp::DocumentUri uriForRelPath(const std::string& relPath) {
    return lsp::DocumentUri(lsp::Uri::parse("file:" + relPath));
}

std::string expectedUriPath(const std::string& relPath) {
    return std::string(lsp::FileUri::fromPath(relPath).path());
}

SymbolDatabase& uvmCorpusDb() {
    static Database db(":memory:");
    static SymbolDatabase sdb(db);
    static bool compiled = false;
    if (!compiled) {
        db.initSchema();
        const fs::path root = uvmCorpusRoot();
        const std::string text = readFileOrThrow(root / "uvm.sv");

        CompilationController controller(sdb);
        ProjectConfig cfg;
        cfg.includeDirs = {"."};
        cfg.defines["UVM_NO_DPI"] = "";

        // CompilerDirectiveStripper/SvPreprocessor resolve `include paths
        // relative to the process's current working directory (matching
        // every prior session's probe technique), so the working directory
        // must be the corpus root while compile() runs.
        const fs::path cwd = fs::current_path();
        fs::current_path(root);
        controller.compile("uvm.sv", text, &cfg);
        fs::current_path(cwd);

        compiled = true;
    }
    return sdb;
}
