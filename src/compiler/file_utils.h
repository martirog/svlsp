#pragma once
#include <cstdint>
#include <optional>
#include <string>

// Reads the entire contents of `path`. Returns std::nullopt if the file
// cannot be opened (missing, permissions, etc.) rather than throwing —
// callers (ProjectCompiler, LibraryResolver) treat a missing file as "skip",
// not a hard error.
std::optional<std::string> readFile(const std::string& path);

// A file's size and modification time (ns since the epoch), the cheap
// first check of whether it changed (plan.md §6.34 S1).
struct FileStat {
    int64_t size{-1};
    int64_t mtimeNs{0};
};

// `path`'s stat data, or nullopt when it can't be stat'ed (missing).
std::optional<FileStat> statFile(const std::string& path);

// The wall clock in ns since the epoch, the scale of FileStat::mtimeNs.
int64_t nowNs();
