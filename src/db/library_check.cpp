#include "db/library_check.h"
#include "db/database.h"
#include "compiler/content_hash.h"
#include "compiler/file_utils.h"
#include <exception>

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
