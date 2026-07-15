#pragma once
#include <optional>
#include <string>

// Reads the entire contents of `path`. Returns std::nullopt if the file
// cannot be opened (missing, permissions, etc.) rather than throwing —
// callers (ProjectCompiler, LibraryResolver) treat a missing file as "skip",
// not a hard error.
std::optional<std::string> readFile(const std::string& path);
