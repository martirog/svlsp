#pragma once
#include <string>
#include <string_view>

// The content hash svlsp stores for a file (plan.md §6.34 S1): SHA-256 of
// the bytes, as 64 lowercase hex digits. The same in every process and
// every build, unlike std::hash, so a hash stored in a DB file still means
// something after a restart, or when another svlsp reads the DB.
std::string contentHash(std::string_view data);
