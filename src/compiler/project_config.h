#pragma once
#include <map>
#include <string>
#include <vector>

// Shared multi-file project configuration, produced by either
// FilelistParser (.f) or ProjectManifestParser (.svlsp.json). Neither
// parser depends on the other; this struct is the entire sharing mechanism.
enum class SvLanguageMode { SystemVerilog, Verilog95 };

struct ProjectConfig {
    std::vector<std::string> files;              // absolute paths
    std::map<std::string, std::string> defines;  // NAME -> VALUE ("" = flag)
    std::vector<std::string> includeDirs;
    std::string topModule;                        // "" if unspecified
    SvLanguageMode mode{SvLanguageMode::SystemVerilog};
    std::vector<std::string> libraryDirs;          // -y
    std::vector<std::string> libraryFiles;         // -v
    std::vector<std::string> libExtensions;        // +libext+ search order
};
