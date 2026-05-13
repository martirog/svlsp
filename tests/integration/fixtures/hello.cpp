// hello.cpp — minimal C++ file used to validate the Emacs/clangd test harness.
// It must compile cleanly; clangd is expected to report no diagnostics on it.

#include <string>

std::string greet(const std::string& name) {
    return "Hello, " + name + "!";
}
