#include "db/library_resolver.h"
#include "db/compilation_controller.h"
#include "db/symbol_database.h"
#include "compiler/compiler_directive_stripper.h"
#include "compiler/file_utils.h"
#include "compiler/sv_preprocessor.h"
#include "compiler/sv_tree_walker.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

// Module/Interface/Program names declared in `text` (a raw parse — nothing
// is persisted to the DB here). Honors the project's includeDirs/defines so
// `-v`/`-y` files preprocess consistently with the rest of the project.
std::vector<std::string> declaredTypeNames(const std::string& text, const std::string& path,
                                            const ProjectConfig& config) {
    auto stripped = CompilerDirectiveStripper::strip(text, path);
    SvPreprocessor pp(config.includeDirs);
    for (const auto& [name, value] : config.defines) pp.define(name, value);
    auto preprocessed = pp.process(stripped.source, path);
    auto walked       = SvTreeWalker::walk(preprocessed.source, preprocessed.sourceMap);

    std::vector<std::string> names;
    for (const auto& r : walked.records) {
        if (r.kind == ParseRecordKind::Module || r.kind == ParseRecordKind::Interface ||
            r.kind == ParseRecordKind::Program) {
            names.push_back(r.name);
        }
    }
    return names;
}

// Searches `-y` directories x `+libext+` extensions, in declared order, for
// `dir/<name><ext>`. Returns {path, content} of the first match.
std::optional<std::pair<std::string, std::string>> searchLibraryDirs(
    const std::string& name, const ProjectConfig& config) {
    for (const auto& dir : config.libraryDirs) {
        for (const auto& ext : config.libExtensions) {
            std::string candidate = dir + "/" + name + ext;
            if (auto text = readFile(candidate)) {
                return std::make_pair(candidate, std::move(*text));
            }
        }
    }
    return std::nullopt;
}

} // namespace

int LibraryResolver::resolve(const ProjectConfig& config, CompilationController& controller,
                              SymbolDatabase& sdb) {
    // Pre-index -v files' declared names. Not compiled/persisted unless a
    // name is actually resolved against them below.
    std::unordered_map<std::string, std::string> vIndex;    // type name -> path
    std::unordered_map<std::string, std::string> vContent;  // path -> content
    for (const auto& vPath : config.libraryFiles) {
        auto text = readFile(vPath);
        if (!text) continue;
        for (const auto& name : declaredTypeNames(*text, vPath, config)) {
            vIndex.try_emplace(name, vPath); // first -v file wins on a name clash
        }
        vContent[vPath] = *text;
    }

    std::unordered_set<std::string> failedNames;
    std::unordered_set<std::string> compiledPaths;
    int compiledCount = 0;

    while (true) {
        auto unresolved = sdb.unresolvedInstantiatedTypeNames();
        std::string name;
        for (const auto& n : unresolved) {
            if (!failedNames.count(n)) { name = n; break; }
        }
        if (name.empty()) break; // nothing left to attempt

        std::string resolvedPath, resolvedText;
        if (auto it = vIndex.find(name); it != vIndex.end()) {
            resolvedPath = it->second;
            resolvedText = vContent[resolvedPath];
        } else if (auto found = searchLibraryDirs(name, config)) {
            resolvedPath = found->first;
            resolvedText = found->second;
        }

        if (resolvedPath.empty() || compiledPaths.count(resolvedPath)) {
            // Either nowhere to find it, or we already compiled the one file
            // that could have declared it and the name is still unresolved
            // (declared under a different name, or not there at all) --
            // don't retry, or this would loop forever.
            failedNames.insert(name);
            continue;
        }

        controller.compile(resolvedPath, resolvedText, &config);
        compiledPaths.insert(resolvedPath);
        ++compiledCount;
    }

    for (const auto& name : failedNames) {
        std::unordered_map<int64_t, std::vector<ParseError>> byFile;
        for (const auto& ref : sdb.instantiationsOfType(name)) {
            byFile[ref.fileId].push_back(
                {ref.line, 0,
                 "Unresolved instantiation of '" + name + "': not found in project files, "
                 "-v library files, or -y library directories."});
        }
        for (const auto& [fileId, errs] : byFile) sdb.appendDiagnostics(fileId, errs);
    }

    return compiledCount;
}
