#pragma once

// `svlsp --socket` (plan.md §6.38): what the editor starts. It reads the
// editor's `initialize` to learn the project root, connects to that root's
// server (`svlsp --daemon`), starting one detached if none listens, and
// then copies stdin -> socket and socket -> stdout until either side
// closes. The editor still talks stdio: only its command line changes.

#include <chrono>
#include <string>
#include <vector>

struct RelayOptions {
    std::string root;       // --root; "" = the initialize's root, else the cwd
    std::string socketPath; // "" = unix_socket::defaultPath(root)
    std::string exe;        // the svlsp binary a server is started with
    std::vector<std::string> serverArgs; // passed on to a started server
    std::chrono::milliseconds startTimeout{std::chrono::seconds{10}};
};

// Returns the exit code. `in` / `out` are the editor's ends (0 / 1).
int runRelay(const RelayOptions& options, int in = 0, int out = 1);

// The project root an `initialize` request's JSON names: rootUri, else
// rootPath, else the first workspace folder; "" when none (or not JSON).
std::string rootFromInitialize(const std::string& json);

// Where a server started for `socketPath` writes its log: the socket's
// path with ".sock" replaced by ".log" (else ".log" appended).
std::string logPathFor(const std::string& socketPath);
