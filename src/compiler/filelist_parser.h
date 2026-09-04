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
//   -svlsp_library_db FILE prebuilt library DB to ATTACH read-only (plan.md
//                          §6.19); chainable (one path per occurrence, like
//                          -v). svlsp-specific, deliberately not a `+switch+`
//                          spelling -- see plan.md §6.19 for why.
//   -svlsp_library_db_source CONFIG CACHE
//                          build-and-cache a library DB on demand (plan.md
//                          §6.19 piece 3): CONFIG is another .svlsp.json/.f
//                          describing the library's own files; CACHE is
//                          where the built DB should be looked for/written.
//                          Chainable, same svlsp-specific reasoning as
//                          -svlsp_library_db.
//   -top MODULE            top module name
//   // ...                 line comment (rest of line ignored)
//   "quoted path"           a single token, spaces allowed inside
//
// The top-level call's own base directory is `baseDir` if given, else the
// process's current working directory (matching `-f` semantics for the
// entry point itself -- the CLI-invocation default). Callers that discover
// a filelist rather than being handed one on a command line (ProjectRegistry's
// auto-discovery) should pass the filelist's own directory explicitly, since
// the server process's CWD has no relation to where a discovered project
// file happens to live.
class FilelistParser {
public:
    // Throws std::runtime_error("<path>:<line>: <reason>") on any
    // unsupported switch or on a -f/-F include cycle.
    static ProjectConfig parse(const std::string& path, const std::string& baseDir = "");
};
