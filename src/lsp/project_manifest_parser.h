#pragma once
#include "compiler/project_config.h"
#include <string>

// Parses a custom `.svlsp.json` project manifest into a ProjectConfig. All
// keys are optional:
//
//   {
//     "files":         ["a.sv", "b.sv"],
//     "defines":       {"WIDTH": "8"},
//     "includeDirs":   ["rtl/include"],
//     "top":           "top_module",
//     "mode":          "sv",           // "sv" (default) or "v95"
//     "libraryDirs":   ["rtl/lib"],
//     "libraryFiles":  ["vendor/ip.v"],
//     "libExtensions": [".sv", ".v"],
//     "libraryDbs":    ["/shared/uvm-1.2.db"]
//   }
//
// Relative paths ("files", "includeDirs", "libraryDirs", "libraryFiles",
// "libraryDbs") resolve against the manifest's own directory. Unknown top-level keys are
// silently ignored (forward-compatible — unlike the filelist parser's
// hard-error policy, a typo'd JSON key can't misparse an unrelated field).
class ProjectManifestParser {
public:
    // Throws std::runtime_error on malformed JSON or a wrongly-typed/invalid
    // known field.
    static ProjectConfig parse(const std::string& path);
};
