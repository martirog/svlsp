#include <lsp/io/standardio.h>
#include "lsp/server.h"

// Entry point — reads LSP JSON-RPC from stdin, writes responses to stdout.
// stderr is reserved for diagnostic logging (lsp-mode ignores it).
int main()
{
    auto& io = lsp::io::standardIO();
    LanguageServer server(io);
    return server.run();
}
