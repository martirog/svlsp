#include "compiler/parse_cache.h"

bool ParseCache::isUpToDate(const std::string& uri, const std::string& text) const
{
    auto it = m_cache.find(uri);
    if (it == m_cache.end()) return false;
    return it->second.contentHash == std::hash<std::string>{}(text);
}

void ParseCache::store(const std::string& uri, const std::string& text, WalkResult result)
{
    m_cache[uri] = {std::hash<std::string>{}(text), std::move(result)};
}

const WalkResult& ParseCache::get(const std::string& uri) const
{
    return m_cache.at(uri).result;
}

void ParseCache::evict(const std::string& uri)
{
    m_cache.erase(uri);
}
