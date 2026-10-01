#pragma once
#include "compiler/project_config.h"

class CompilationController;
class SymbolDatabase;

// Fixpoint resolution of module/interface/program instantiations that
// aren't declared anywhere in the currently-compiled files, against a
// project's `-v` library files and `-y` library directories.
class LibraryResolver {
public:
    // Repeatedly asks `sdb` for unresolved instantiated type names and tries
    // to resolve each one: first against `config.libraryFiles` (-v, checked
    // by pre-parsing each file's declared Module/Interface/Program names —
    // not persisted until actually referenced), then by searching
    // `config.libraryDirs` (-y) x `config.libExtensions` (+libext+) in
    // declared order for `dir/<Name><ext>`. Every resolved file is compiled
    // via `controller`, which can surface further unresolved instantiations
    // that feed the next round. A name that fails to resolve is never
    // retried; combined with `controller.compile`'s content-hash cache, this
    // guarantees the loop terminates. Every name still unresolved when the
    // loop ends gets a diagnostic attached (via SymbolDatabase::
    // appendDiagnostics, source 'library', subject the name) to each file
    // that references it; a later recompile of that file keeps and
    // re-anchors it (SymbolDatabase::refreshLibraryDiagnostics).
    //
    // Returns the number of additional files compiled.
    static int resolve(const ProjectConfig& config, CompilationController& controller,
                        SymbolDatabase& sdb);
};
