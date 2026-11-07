#pragma once
#include "compiler/project_config.h"
#include "db/project_refresher.h"
#include <string>

class CompilationController;
class SymbolDatabase;

// Batch-compiles a project's explicit file list, then hands off to
// LibraryResolver to pull in any -v/-y library files needed by unresolved
// instantiations.
class ProjectCompiler {
public:
    // ATTACHes config.libraryDbs read-only onto sdb (plan.md §6.19; a no-op
    // if empty); merges each attached library DB's own stored includeDirs
    // (plan.md §6.19 piece 4 -- see SymbolDatabase::libraryIncludeDirs) into
    // config.includeDirs, so a project's own `` `include ``s of that
    // library's headers (e.g. UVM's macro headers) resolve too, not just
    // its symbols; compiles every path in config.files (missing files are
    // skipped, not an error); then runs LibraryResolver::resolve.
    // `config` is mutated in place by the includeDirs merge above -- takes
    // it by non-const reference for that reason, not just to read it.
    // The compile is a ProjectRefresher refresh of project `projectKey` (the
    // config's absolute path): on a DB that already holds the project, only
    // what changed is compiled (plan.md §6.34 S2). `result`, when non-null,
    // receives the refresh's result. Returns the number of units compiled
    // (explicit + library-resolved).
    // loadProject's first step alone: attaches config.libraryDbs and merges
    // their stored includeDirs into config.includeDirs.
    static void attachLibraries(ProjectConfig& config, SymbolDatabase& sdb);

    static int loadProject(ProjectConfig& config, CompilationController& controller,
                           SymbolDatabase& sdb, const std::string& projectKey = "",
                           const std::string& svlspVersion = "",
                           ProjectRefresher::Result* result = nullptr, int jobs = 1);
};
