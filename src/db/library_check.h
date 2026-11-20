#pragma once
#include <string>
#include <vector>

// Whether a library DB built by `svlsp --build-db` still matches the files
// it was built from (plan.md §6.34 S7, §6.19's staleness question). Each
// file the DB recorded is stat'ed; only a file whose size or mtime differs
// from the recorded ones (or whose stat was taken too close to its mtime to
// trust) is read and hashed. A DB built before schema v17 recorded no stat
// data: its files count as `unknown`, never as stale.
struct LibraryStatus {
    int total{0};    // files the DB recorded
    int changed{0};  // content differs from what was compiled
    int missing{0};  // gone from disk
    int unknown{0};  // no stat data recorded (built before v17)
    int read{0};     // files read to tell (the rest by stat alone)
    std::vector<std::string> examples; // a few changed or missing paths
    bool stale() const { return changed > 0 || missing > 0; }
    // "3 of 166 files changed since it was built" (or "" when fresh).
    std::string describe() const;
};

// Checks the library DB at `dbPath` (opened read-only, never migrated).
// A DB that can't be opened is reported with total 0.
LibraryStatus checkLibraryDb(const std::string& dbPath);

// What a library DB holds, for reporting it (plan.md §6.40 D1). Looking
// never creates the file: SQLite's ATTACH would, silently, for a path
// whose directory exists.
struct LibraryDbInfo {
    bool exists{false};
    std::string error; // why it can't be read as an svlsp DB ("" if it can)
    int schema{0};
    int files{0};
    int symbols{0};
    std::vector<std::string> missingIncludeDirs; // recorded at build time, not here
};

// Opens `dbPath` read-only when it is a file and counts what it holds.
LibraryDbInfo inspectLibraryDb(const std::string& dbPath);

// What makes it unusable or suspect, each a phrase to follow its path
// ("does not exist"); empty when it is fine.
std::vector<std::string> libraryDbProblems(const LibraryDbInfo& info);

// "152 files, 4120 symbols".
std::string libraryDbSummary(const LibraryDbInfo& info);
