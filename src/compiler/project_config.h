#pragma once
#include <filesystem>
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
    std::string database;                          // -svlsp_db / "database": the project's
                                                    // own DB file ("" = in memory;
                                                    // plan.md §6.34 S3)
    std::vector<std::string> preincludes;          // -svlsp_preinclude / "preincludes":
                                                    // headers read before every unit's
                                                    // own text, like gcc -include
                                                    // (plan.md §6.40 D8)
};

// A pre-include as the config names it: the path beside the config when a
// file is there (or it is absolute), else the name as written, which the
// include dirs resolve at compile time as for an `include.
inline std::string resolvePreinclude(const std::string& baseDir, const std::string& raw)
{
    namespace fs = std::filesystem;
    if (fs::path(raw).is_absolute()) return fs::path(raw).lexically_normal().string();
    const fs::path beside = fs::path(baseDir) / raw;
    std::error_code ec;
    if (fs::is_regular_file(beside, ec)) return beside.lexically_normal().string();
    return raw;
}

// Where a pre-include is read from by a unit: absolute, or the first of
// `includeDirs` holding it ("" when none does). A unit also looks beside its
// own file first, as for an `include; this is what the config alone says
// (plan.md §6.40 D8: a missing one is reported once, not in every file).
inline std::string locatePreinclude(const std::string& header,
                                    const std::vector<std::string>& includeDirs)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::path(header).is_absolute()) return fs::is_regular_file(header, ec) ? header : "";
    for (const auto& d : includeDirs) {
        const fs::path p = fs::path(d) / header;
        if (fs::is_regular_file(p, ec)) return p.lexically_normal().string();
    }
    return "";
}
