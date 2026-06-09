#pragma once
#include "compiler/sv_tree_walker.h"
#include <string>
#include <unordered_map>

// In-memory cache mapping document URI (as toString() string) to the
// WalkResult last produced for that content.  A content hash guards each
// entry: if the text passed to isUpToDate matches the stored hash the caller
// can skip the full pipeline and reuse the cached result.
//
// Phase 5.4 will replace this with a SQLite-backed implementation that
// survives across server restarts.
class ParseCache {
public:
    // Returns true if the cached entry for `uri` was produced from `text`.
    bool isUpToDate(const std::string& uri, const std::string& text) const;

    // Store `result` as the current parse output for `uri`/`text`.
    void store(const std::string& uri, const std::string& text, WalkResult result);

    // Return the cached WalkResult.  Caller must confirm isUpToDate first.
    const WalkResult& get(const std::string& uri) const;

    // Remove the entry for `uri` — call on didClose so the next open triggers
    // a fresh parse.
    void evict(const std::string& uri);

private:
    struct Entry {
        std::size_t contentHash;
        WalkResult  result;
    };
    std::unordered_map<std::string, Entry> m_cache;
};
