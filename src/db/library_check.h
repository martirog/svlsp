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
