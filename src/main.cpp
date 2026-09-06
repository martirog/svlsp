#include <lsp/io/standardio.h>
#include "lsp/server.h"
#include "lsp/library_db_builder.h"
#include "lsp/project_manifest_parser.h"
#include "compiler/filelist_parser.h"
#include "compiler/project_config.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <unordered_set>

namespace {

// Renders `LibraryDbBuilder`/`CompilationController`'s per-file progress
// lines ("[parsed] <path>", "[parsed] <path> (cached)", "[parsed]
// included: <path>") as a single, growing, in-place counter on stderr
// instead of a scrolling log -- the total file count isn't known upfront
// (transitive `include`s and -v/-y library resolution both discover more
// files as compilation proceeds), so `total` starts at `floorTotal` (the
// project's own explicit file list -- a known lower bound) and grows to
// match however many distinct files have actually been seen once that
// bound is exceeded. A `std::streambuf` (not a wrapper function) so it can
// be handed anywhere an `std::ostream*` progress log is accepted, with zero
// changes to LibraryDbBuilder/CompilationController/ProjectCompiler.
class ProgressCounterBuf : public std::streambuf {
public:
    explicit ProgressCounterBuf(int floorTotal) : m_floorTotal(floorTotal) {}

    // Call after the compile finishes to erase the last in-place line.
    void finish()
    {
        if (m_rendered) std::cerr << '\r' << std::string(m_lastWidth, ' ') << '\r';
    }

protected:
    int_type overflow(int_type ch) override
    {
        if (traits_type::eq_int_type(ch, traits_type::eof())) return ch;
        char c = traits_type::to_char_type(ch);
        if (c == '\n') {
            handleLine(m_line);
            m_line.clear();
        } else {
            m_line.push_back(c);
        }
        return ch;
    }

private:
    void handleLine(const std::string& line)
    {
        static const std::string kIncludedPrefix = "[parsed]   included: ";
        static const std::string kParsedPrefix   = "[parsed] ";
        static const std::string kCachedSuffix   = " (cached)";

        std::string path;
        if (line.compare(0, kIncludedPrefix.size(), kIncludedPrefix) == 0) {
            path = line.substr(kIncludedPrefix.size());
        } else if (line.compare(0, kParsedPrefix.size(), kParsedPrefix) == 0) {
            path = line.substr(kParsedPrefix.size());
            if (path.size() >= kCachedSuffix.size() &&
                path.compare(path.size() - kCachedSuffix.size(), kCachedSuffix.size(),
                             kCachedSuffix) == 0)
                path.resize(path.size() - kCachedSuffix.size());
        } else {
            return; // not a per-file progress line -- ignore
        }

        m_seen.insert(std::move(path));
        int total = std::max<int>(m_floorTotal, static_cast<int>(m_seen.size()));

        std::ostringstream rendered;
        rendered << "svlsp: compiling... " << m_seen.size() << "/" << total << " files";
        std::string text = rendered.str();
        std::cerr << '\r' << text;
        if (text.size() < m_lastWidth) std::cerr << std::string(m_lastWidth - text.size(), ' ');
        std::cerr.flush();
        m_lastWidth = text.size();
        m_rendered  = true;
    }

    int m_floorTotal;
    std::string m_line;
    std::unordered_set<std::string> m_seen;
    std::size_t m_lastWidth{0};
    bool m_rendered{false};
};

// The explicit top-level file count from `configPath` (the same dispatch
// LibraryDbBuilder::build uses), or 0 if it can't be parsed -- used only as
// ProgressCounterBuf's starting floor, so a parse failure here just means
// the counter starts at 0 rather than a real error; LibraryDbBuilder::build
// itself reports the actual parse error below.
int explicitFileCount(const std::string& configPath)
{
    try {
        ProjectConfig config = configPath.ends_with(".json")
            ? ProjectManifestParser::parse(configPath)
            : FilelistParser::parse(configPath,
                  std::filesystem::path(configPath).parent_path().string());
        return static_cast<int>(config.files.size());
    } catch (const std::exception&) {
        return 0;
    }
}

// svlsp --build-db <config-path> --output <db-path>: compiles a
// .svlsp.json manifest or .f/.svlsp.f filelist into a persistent SQLite DB
// at <db-path>, then exits -- lets a pre-built library DB (plan.md §6.19)
// be produced ahead of time, independent of any editor session, and
// without ever entering the normal initialize/stdio message loop. The
// actual compile work lives in LibraryDbBuilder (src/lsp/library_db_builder.h),
// factored out so it's unit-testable; this is just the CLI-facing wrapper.
// Returns the process exit code.
int buildDb(const std::string& configPath, const std::string& outputPath)
{
    // Progress feedback goes to stderr as a single growing counter (see
    // ProgressCounterBuf above) -- unlike the server's --log-files (which
    // targets a file and keeps every line, since stdout/stderr are reserved
    // for the LSP client there), this is a one-shot CLI invocation with a
    // real terminal to update in place.
    ProgressCounterBuf progressBuf(explicitFileCount(configPath));
    std::ostream progressStream(&progressBuf);

    auto result = LibraryDbBuilder::build(configPath, outputPath, &progressStream);
    progressBuf.finish();
    if (!result.ok) {
        std::cerr << "svlsp: error parsing '" << configPath << "': " << result.error << '\n';
        return 1;
    }

    std::cerr << "svlsp: built '" << outputPath << "' -- " << result.fileCount
               << " files compiled, " << result.diagnosticCount << " diagnostics\n";
    return 0;
}

} // namespace

// Entry point — reads LSP JSON-RPC from stdin, writes responses to stdout.
// stderr is reserved for diagnostic logging (lsp-mode ignores it).
//
// --log-files <path>: opens <path> (appending) and logs every file the
// compiler parses/persists — the primary file on each didOpen/didChange,
// plus every `include`d file discovered that pass. Lets you confirm a
// multi-file project's full expected file set actually got parsed, rather
// than silently missing files (e.g. a misconfigured include path).
//
// --build-db <config-path> --output <db-path>: an alternate, one-shot mode
// (see buildDb() above) that compiles a project into a persistent DB file
// and exits, instead of starting the normal server loop. `<config-path>`/
// `<db-path>`/`--log-files`'s own `<path>` must each be the literal next
// argument after their flag -- `--build-db --output db.f config.f` (flags
// grouped before their values) is NOT accepted, only `--build-db config.f
// --output db.f` (each flag immediately followed by its own value); see
// plan.md §6.19 and docs/usage.md.
int main(int argc, char** argv)
{
    std::ofstream logFile;
    std::ostream* logStream = nullptr;
    std::string buildDbConfigPath;
    std::string buildDbOutputPath;
    for (int i = 1; i < argc; ++i) {
        // A flag's value is required to be the very next argv entry; if
        // that's missing, or looks like another flag (starts with "--"),
        // don't silently swallow it as the value (the original bug this
        // guards against: `--build-db --output out.db config.f` used to
        // consume the literal string "--output" as the config path, leave
        // `--output`'s own real value/`config.f` unparsed, and fail later
        // with a confusing "--build-db requires --output" even though
        // --output was right there).
        const bool nextIsMissingOrFlag =
            i + 1 >= argc || (argv[i + 1][0] == '-' && argv[i + 1][1] == '-');

        if (std::strcmp(argv[i], "--log-files") == 0) {
            if (nextIsMissingOrFlag) {
                std::cerr << "svlsp: --log-files requires a <path> argument "
                             "immediately after it\n";
                return 1;
            }
            const char* logPath = argv[++i];
            logFile.open(logPath, std::ios::out | std::ios::app);
            if (logFile)
                logStream = &logFile;
            else
                std::cerr << "svlsp: warning: could not open --log-files path '"
                          << logPath << "'\n";
        } else if (std::strcmp(argv[i], "--build-db") == 0) {
            if (nextIsMissingOrFlag) {
                std::cerr << "svlsp: --build-db requires a <config-path> argument "
                             "immediately after it (put --output after that, not "
                             "before it)\n";
                return 1;
            }
            buildDbConfigPath = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0) {
            if (nextIsMissingOrFlag) {
                std::cerr << "svlsp: --output requires a <db-path> argument "
                             "immediately after it\n";
                return 1;
            }
            buildDbOutputPath = argv[++i];
        }
    }

    if (!buildDbConfigPath.empty()) {
        if (buildDbOutputPath.empty()) {
            std::cerr << "svlsp: --build-db requires --output <db-path>\n";
            return 1;
        }
        return buildDb(buildDbConfigPath, buildDbOutputPath);
    }

    auto& io = lsp::io::standardIO();
    LanguageServer server(io, logStream);
    return server.run();
}
