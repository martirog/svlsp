#pragma once
#include <map>
#include <string>
#include <vector>

// Shared multi-file project configuration, produced by either
// FilelistParser (.f) or ProjectManifestParser (.svlsp.json). Neither
// parser depends on the other; this struct is the entire sharing mechanism.
enum class SvLanguageMode { SystemVerilog, Verilog95 };

// A library whose DB should be looked for at `cachePath`, building it from
// `configPath` (another .svlsp.json/.f describing the library's own files)
// first if it isn't there yet -- plan.md §6.19 piece 3, "build me one on
// demand and remember it". Resolved by LibraryDbBuilder::resolveLibraryDbSources
// (src/lsp/library_db_builder.h) into an ordinary attached libraryDbs entry.
struct LibraryDbSource {
    std::string configPath;
    std::string cachePath;
};

struct ProjectConfig {
    std::vector<std::string> files;              // absolute paths
    std::map<std::string, std::string> defines;  // NAME -> VALUE ("" = flag)
    std::vector<std::string> includeDirs;
    std::string topModule;                        // "" if unspecified
    SvLanguageMode mode{SvLanguageMode::SystemVerilog};
    std::vector<std::string> libraryDirs;          // -y
    std::vector<std::string> libraryFiles;         // -v
    std::vector<std::string> libExtensions;        // +libext+ search order
    std::vector<std::string> libraryDbs;           // -svlsp_library_db / "libraryDbs" --
                                                    // prebuilt library DBs to ATTACH
                                                    // read-only (plan.md §6.19)
    std::vector<LibraryDbSource> libraryDbSources; // -svlsp_library_db_source /
                                                    // "libraryDbSources" -- build-and-cache
                                                    // on first use (plan.md §6.19 piece 3)
};
