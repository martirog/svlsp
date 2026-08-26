#include <lsp/io/standardio.h>
#include "lsp/server.h"
#include <cstring>
#include <fstream>
#include <iostream>

// Entry point — reads LSP JSON-RPC from stdin, writes responses to stdout.
// stderr is reserved for diagnostic logging (lsp-mode ignores it).
//
// --log-files <path>: opens <path> (appending) and logs every file the
// compiler parses/persists — the primary file on each didOpen/didChange,
// plus every `include`d file discovered that pass. Lets you confirm a
// multi-file project's full expected file set actually got parsed, rather
// than silently missing files (e.g. a misconfigured include path).
int main(int argc, char** argv)
{
    std::ofstream logFile;
    std::ostream* logStream = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--log-files") == 0 && i + 1 < argc) {
            const char* logPath = argv[++i];
            logFile.open(logPath, std::ios::out | std::ios::app);
            if (logFile)
                logStream = &logFile;
            else
                std::cerr << "svlsp: warning: could not open --log-files path '"
                          << logPath << "'\n";
        }
    }

    auto& io = lsp::io::standardIO();
    LanguageServer server(io, logStream);
    return server.run();
}
