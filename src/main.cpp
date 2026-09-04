#include <lsp/io/standardio.h>
#include "lsp/server.h"
#include "lsp/library_db_builder.h"
#include <cstring>
#include <fstream>
#include <iostream>

namespace {

// svlsp --build-db <config-path> --output <db-path>: compiles a
// .svlsp.json manifest or .f/.svlsp.f filelist into a persistent SQLite DB
// at <db-path>, then exits -- lets a pre-built library DB (plan.md §6.19)
// be produced ahead of time, independent of any editor session, and
// without ever entering the normal initialize/stdio message loop. The
// actual compile work lives in LibraryDbBuilder (src/lsp/library_db_builder.h),
// factored out so it's unit-testable; this is just the CLI-facing wrapper.
// Returns the process exit code.
int buildDb(const std::string& configPath, const std::string& outputPath)
{
    // Logs every file parsed to stderr -- unlike the server's --log-files
    // (which targets a file, since stdout/stderr are reserved for the LSP
    // client), this is a one-shot CLI invocation with no client to disturb,
    // so progress feedback goes straight to stderr.
    auto result = LibraryDbBuilder::build(configPath, outputPath, &std::cerr);
    if (!result.ok) {
        std::cerr << "svlsp: error parsing '" << configPath << "': " << result.error << '\n';
        return 1;
    }

    std::cerr << "svlsp: built '" << outputPath << "' -- " << result.fileCount
               << " files compiled, " << result.diagnosticCount << " diagnostics\n";
    return 0;
}

} // namespace

// Entry point — reads LSP JSON-RPC from stdin, writes responses to stdout.
// stderr is reserved for diagnostic logging (lsp-mode ignores it).
//
// --log-files <path>: opens <path> (appending) and logs every file the
// compiler parses/persists — the primary file on each didOpen/didChange,
// plus every `include`d file discovered that pass. Lets you confirm a
// multi-file project's full expected file set actually got parsed, rather
// than silently missing files (e.g. a misconfigured include path).
//
// --build-db <config-path> --output <db-path>: an alternate, one-shot mode
// (see buildDb() above) that compiles a project into a persistent DB file
// and exits, instead of starting the normal server loop. See plan.md §6.19
// and docs/usage.md.
int main(int argc, char** argv)
{
    std::ofstream logFile;
    std::ostream* logStream = nullptr;
    std::string buildDbConfigPath;
    std::string buildDbOutputPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--log-files") == 0 && i + 1 < argc) {
            const char* logPath = argv[++i];
            logFile.open(logPath, std::ios::out | std::ios::app);
            if (logFile)
                logStream = &logFile;
            else
                std::cerr << "svlsp: warning: could not open --log-files path '"
                          << logPath << "'\n";
        } else if (std::strcmp(argv[i], "--build-db") == 0 && i + 1 < argc) {
            buildDbConfigPath = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            buildDbOutputPath = argv[++i];
        }
    }

    if (!buildDbConfigPath.empty()) {
        if (buildDbOutputPath.empty()) {
            std::cerr << "svlsp: --build-db requires --output <db-path>\n";
            return 1;
        }
        return buildDb(buildDbConfigPath, buildDbOutputPath);
    }

    auto& io = lsp::io::standardIO();
    LanguageServer server(io, logStream);
    return server.run();
}
