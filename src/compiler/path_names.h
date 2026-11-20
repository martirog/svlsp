#pragma once
#include <string>

// One name per file (plan.md §6.40 D6). Inside svlsp a file is named by its
// canonical path: absolute, symlinks resolved (for the part of the path
// that exists), `.` and `..` resolved as the OS would. A config, an include
// directory and an `include` that reach the same file through different
// symlinks then agree, so an included file opened in the editor finds the
// unit that includes it.
//
// The client names files its own way (Emacs sends the path as opened,
// symlinks kept), and a URI the server sends back must use that name.
// fromClient() records the client's name for a file and for each directory
// above it that differs from its canonical one; toClient() maps a
// canonical path back: the file's own recorded name, else the deepest
// recorded directory with the rest appended, else the canonical path.

namespace pathnames {

// The canonical name of `path` (made absolute against the cwd first).
// A path that can't be resolved at all is returned absolute and
// lexically normalized.
std::string canonical(const std::string& path);

// canonical(clientPath), recording clientPath as the client's name for it.
std::string fromClient(const std::string& clientPath);

// The client's name for canonical path `path` (see above).
std::string toClient(const std::string& path);

// Forgets every recorded client name (tests).
void clearClientNames();

} // namespace pathnames
