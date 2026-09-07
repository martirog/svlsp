#pragma once
#include "compiler/project_config.h"

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
    // its symbols; reads and compiles every path in config.files (missing
    // files are skipped, not an error); then runs LibraryResolver::resolve.
    // `config` is mutated in place by the includeDirs merge above -- takes
    // it by non-const reference for that reason, not just to read it.
    // Returns the total number of files compiled (explicit + library-resolved).
    static int loadProject(ProjectConfig& config, CompilationController& controller,
                           SymbolDatabase& sdb);
};
