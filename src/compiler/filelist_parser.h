#pragma once
#include "compiler/project_config.h"
#include <string>

// Parses a VCS/Questa/Xcelium-style `.f` filelist into a ProjectConfig.
//
// Supported syntax (any other `-x`/`+x` switch is a hard error):
//   bareword               source filename (relative paths resolve against
//                          the current file's base directory — see below)
//   +define+NAME[=VALUE]   chainable on '+'; maps to a preprocessor define
//   +incdir+DIR            chainable on '+'; include directory
//   +libext+.ext           chainable on '+'; -y search extension, in order
//   -f FILE                nested filelist; FILE and paths inside it resolve
//                          against the CURRENT base directory (unchanged)
//   -F FILE                nested filelist; FILE resolves against the
//                          current base directory, but paths inside it
//                          resolve against FILE's own directory (changed)
//   -sv / -sverilog        SystemVerilog mode (the default; explicit no-op)
//   -y DIR                 library directory (searched by module name + ext)
//   -v FILE                library file (only compiled if referenced)
//   -top MODULE            top module name
//   // ...                 line comment (rest of line ignored)
//   "quoted path"           a single token, spaces allowed inside
//
// The top-level call's own base directory is the process's current working
// directory (matching `-f` semantics for the entry point itself).
class FilelistParser {
public:
    // Throws std::runtime_error("<path>:<line>: <reason>") on any
    // unsupported switch or on a -f/-F include cycle.
    static ProjectConfig parse(const std::string& path);
};
