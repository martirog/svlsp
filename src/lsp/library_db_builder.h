#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

// Compiles a `.svlsp.json` manifest or `.f`/`.svlsp.f` filelist into a
// persistent SQLite database at a given path -- the engine behind
// `svlsp --build-db` (plan.md §6.19), factored out of main.cpp so it's
// unit-testable, and so a future in-process caller (e.g. §6.19's lazy
// build-and-cache path, which needs to build a library DB on demand rather
// than only via a separate CLI invocation) can reuse it directly.
class LibraryDbBuilder {
public:
    struct Result {
        bool        ok{false};
        int         fileCount{0};
        int64_t     diagnosticCount{0};
        std::string error; // set only when !ok
    };

    // Dispatches `configPath` to ProjectManifestParser or FilelistParser by
    // extension (`.json` vs everything else -- the same rule ProjectRegistry
    // already uses), compiles every resulting file into a fresh Database at
    // `outputPath`, and returns a summary. `progressLog`, when non-null,
    // receives one line per file parsed (same shape as
    // CompilationController's own logStream parameter). On a config parse
    // failure, returns `{ok=false, error=<message>}` without creating
    // `outputPath` at all.
    static Result build(const std::string& configPath, const std::string& outputPath,
                        std::ostream* progressLog = nullptr);
};
