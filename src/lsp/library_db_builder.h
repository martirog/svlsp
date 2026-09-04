#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

struct ProjectConfig;

// Compiles a `.svlsp.json` manifest or `.f`/`.svlsp.f` filelist into a
// persistent SQLite database at a given path -- the engine behind
// `svlsp --build-db` (plan.md §6.19), factored out of main.cpp so it's
// unit-testable, and so an in-process caller (§6.19 piece 3's lazy
// build-and-cache path, ProjectRegistry) can reuse it directly rather than
// only via a separate CLI invocation.
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
    // `outputPath` at all. Resolves `configPath`'s own `libraryDbSources`
    // first (see resolveLibraryDbSources below), so a library being built
    // can itself depend on other lazily-cached libraries.
    static Result build(const std::string& configPath, const std::string& outputPath,
                        std::ostream* progressLog = nullptr);

    // For each `config.libraryDbSources` entry (plan.md §6.19 piece 3):
    // if its cachePath already exists on disk, leave it alone; otherwise
    // build it from its configPath via build() above. Either way, appends
    // cachePath to `config.libraryDbs` so the ordinary attach-and-query
    // machinery (SymbolDatabase::attachLibraryDbs, wired in via
    // ProjectCompiler::loadProject) picks it up exactly like a directly
    // referenced prebuilt DB (piece 2) -- piece 3 subsumes piece 2 this
    // way, per plan.md §6.19's own design. Called from both build() itself
    // (so a library's own config can transitively depend on further
    // lazily-cached libraries) and ProjectRegistry (for the live server).
    // Throws std::runtime_error if building a missing cache fails.
    // Deliberately does NOT detect a dependency cycle between configs (e.g.
    // A's libraryDbSources building B, whose own libraryDbSources builds
    // A) -- disclosed, not fixed, in this first cut.
    static void resolveLibraryDbSources(ProjectConfig& config,
                                        std::ostream* progressLog = nullptr);
};
