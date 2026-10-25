// pp_dump -- preprocess one SystemVerilog file the way CompilationController
// does (CompilerDirectiveStripper, then SvPreprocessor) and write the result,
// the exact text the parser sees, to stdout.
//
//   pp_dump [-I dir]... [-D name[=value]]... file.sv > file.pp.sv
//
// Used by tools/grammar_profile.sh to make the fixed input of the grammar
// profile (plan.md §6.33 C2); the parser itself is built separately, from
// whatever grammar copy is being profiled.
#include "compiler/compiler_directive_stripper.h"
#include "compiler/sv_preprocessor.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> includes;
    std::vector<std::pair<std::string, std::string>> defines;
    std::string file;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-I" && i + 1 < argc) {
            includes.push_back(argv[++i]);
        } else if (a == "-D" && i + 1 < argc) {
            const std::string d = argv[++i];
            const auto eq = d.find('=');
            defines.emplace_back(d.substr(0, eq), eq == std::string::npos ? "" : d.substr(eq + 1));
        } else if (file.empty()) {
            file = a;
        } else {
            std::cerr << "usage: pp_dump [-I dir]... [-D name[=value]]... file.sv\n";
            return 2;
        }
    }
    if (file.empty()) {
        std::cerr << "usage: pp_dump [-I dir]... [-D name[=value]]... file.sv\n";
        return 2;
    }
    std::ifstream in(file);
    if (!in) {
        std::cerr << "pp_dump: cannot read " << file << "\n";
        return 1;
    }
    std::stringstream ss;
    ss << in.rdbuf();

    auto stripped = CompilerDirectiveStripper::strip(ss.str(), file);
    SvPreprocessor pp(includes);
    for (const auto& [name, value] : defines) pp.define(name, value);
    auto result = pp.process(stripped.source, file);
    for (const auto& e : result.errors) std::cerr << "pp_dump: " << e << "\n";
    std::cout << result.source;
    return 0;
}
