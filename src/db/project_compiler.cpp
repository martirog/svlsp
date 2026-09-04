#include "db/project_compiler.h"
#include "db/compilation_controller.h"
#include "db/library_resolver.h"
#include "compiler/file_utils.h"

int ProjectCompiler::loadProject(const ProjectConfig& config, CompilationController& controller,
                                 SymbolDatabase& sdb) {
    sdb.attachLibraryDbs(config.libraryDbs);

    int compiledCount = 0;
    for (const auto& path : config.files) {
        if (auto text = readFile(path)) {
            controller.compile(path, *text, &config);
            ++compiledCount;
        }
    }
    compiledCount += LibraryResolver::resolve(config, controller, sdb);
    return compiledCount;
}
