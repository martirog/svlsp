#include "db/library_check.h"
#include "db/database.h"
#include "compiler/content_hash.h"
#include "compiler/file_utils.h"
#include "db/schema.h"
#include "db/symbol_database.h"
#include <exception>
#include <filesystem>

std::string LibraryStatus::describe() const
{
    if (!stale()) return "";
    std::string out = std::to_string(changed + missing) + " of " + std::to_string(total) +
                      " files changed since it was built";
    if (missing) out += " (" + std::to_string(missing) + " gone)";
    if (!examples.empty()) {
        out += ", e.g. " + examples.front();
    }
    return out;
}

LibraryStatus checkLibraryDb(const std::string& dbPath)
{
    LibraryStatus st;
    try {
        Database db(":memory:");
        db.reopen(dbPath, /*readOnly=*/true);
        // A DB older than v17 has no stat columns.
        auto cols = db.prepare("SELECT 1 FROM pragma_table_info('files') WHERE name = 'mtime_ns'");
        const bool hasStat = cols.step();
        auto rows = db.prepare(hasStat
            ? "SELECT path, content_hash, size, mtime_ns, stat_ns FROM files"
            : "SELECT path, content_hash, -1, 0, 0 FROM files");
        while (rows.step()) {
            ++st.total;
            const std::string path = rows.columnText(0);
            const std::string hash = rows.columnText(1);
            const int64_t size = rows.columnInt(2), mtime = rows.columnInt(3),
                          statNs = rows.columnInt(4);
            const auto now = statFile(path);
            auto note = [&](int& counter) {
                ++counter;
                if (st.examples.size() < 3) st.examples.push_back(path);
            };
            if (!now) {
                note(st.missing);
                continue;
            }
            if (size < 0 || hash.empty()) {
                ++st.unknown;
                continue;
            }
            if (now->size == size && now->mtimeNs == mtime && mtime + kRacyWindowNs < statNs)
                continue;
            ++st.read;
            const auto text = readFile(path);
            if (!text) note(st.missing);
            else if (contentHash(*text) != hash) note(st.changed);
        }
    } catch (const std::exception&) {
        st = LibraryStatus{};
    }
    return st;
}

LibraryDbInfo inspectLibraryDb(const std::string& dbPath)
{
    namespace fs = std::filesystem;
    LibraryDbInfo info;
    std::error_code ec;
    info.exists = fs::is_regular_file(dbPath, ec);
    if (!info.exists) return info;
    try {
        Database db(":memory:");
        db.reopen(dbPath, /*readOnly=*/true);
        info.schema = db.schemaVersion();
        if (info.schema == 0) {
            info.error = "no svlsp schema in it";
            return info;
        }
        auto count = [&](const char* table) {
            auto stmt = db.prepare(std::string("SELECT COUNT(*) FROM ") + table);
            return stmt.step() ? static_cast<int>(stmt.columnInt(0)) : 0;
        };
        info.files   = count("files");
        info.symbols = count("symbols");
        SymbolDatabase sdb(db);
        for (const auto& dir : sdb.libraryIncludeDirs())
            if (!fs::is_directory(dir, ec)) info.missingIncludeDirs.push_back(dir);
    } catch (const std::exception& e) {
        info.error = e.what();
    }
    return info;
}

std::vector<std::string> libraryDbProblems(const LibraryDbInfo& info)
{
    if (!info.exists) return {"does not exist"};
    if (!info.error.empty()) return {"can't be read as an svlsp database: " + info.error};
    std::vector<std::string> out;
    if (info.schema > db::SCHEMA_VERSION)
        out.push_back("was built by a newer svlsp (schema " + std::to_string(info.schema) +
                      ", this one reads up to " + std::to_string(db::SCHEMA_VERSION) +
                      "): rebuild it with this svlsp's --build-db");
    if (info.files == 0)
        out.push_back("holds no files (an empty database: a wrong path, or a failed --build-db?)");
    if (!info.missingIncludeDirs.empty()) {
        std::string dirs;
        for (const auto& d : info.missingIncludeDirs) dirs += (dirs.empty() ? "" : ", ") + d;
        out.push_back("records include dirs that don't exist here: " + dirs +
                      " (built on another machine or from another path?)");
    }
    return out;
}

std::string libraryDbSummary(const LibraryDbInfo& info)
{
    return std::to_string(info.files) + " files, " + std::to_string(info.symbols) + " symbols";
}
