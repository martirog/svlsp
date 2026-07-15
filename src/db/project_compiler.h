#pragma once
#include "compiler/project_config.h"

class CompilationController;
class SymbolDatabase;

// Batch-compiles a project's explicit file list, then hands off to
// LibraryResolver to pull in any -v/-y library files needed by unresolved
// instantiations.
class ProjectCompiler {
public:
    // Reads and compiles every path in config.files (missing files are
    // skipped, not an error), then runs LibraryResolver::resolve. Returns
    // the total number of files compiled (explicit + library-resolved).
    static int loadProject(const ProjectConfig& config, CompilationController& controller,
                           SymbolDatabase& sdb);
};
