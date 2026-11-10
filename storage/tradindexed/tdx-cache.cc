/*
**  Cache functions for open overview data files.
**
**  This code maintains a cache of open overview data files to avoid some of
**  the overhead involved in closing and reopening files.  All opens and
**  closes should go through this code, and the hit ratio is tracked to check
**  cache effectiveness.
*/

#include "portable/system.h"

#include <time.h>

#include <unordered_map>

#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/storage.h"
#include "tdx-private.h"

namespace {
/* A cache entry, holding a group_data struct and the time it was last used,
   which helps choose what to drop from the cache. */
struct cache_entry {
    struct group_data *data;
    time_t lastused;
};

using entry_map = std::unordered_map<HASH, cache_entry, tdx_hash_hasher,
                                     tdx_hash_equal>;


/*
**  Release a cache entry's hold on its group data.
*/
void
entry_release(const cache_entry &entry)
{
    entry.data->refcount--;
    if (entry.data->refcount == 0)
        tdx_data_close(entry.data);
}
} // namespace

/* Returned to callers as an opaque data type, this struct holds all of the
   information about the cache. */
struct cache {
    entry_map entries;
    unsigned int max;
    unsigned long queries;
    unsigned long hits;
};


/*
**  Create a new cache with the given size.
*/
struct cache *
tdx_cache_create(unsigned int size)
{
    auto *cache = new struct cache;

    cache->max = size;
    cache->queries = 0;
    cache->hits = 0;
    cache->entries.reserve(size);
    return cache;
}


/*
**  Look up a particular entry and return it.
*/
struct group_data *
tdx_cache_lookup(struct cache *cache, HASH hash)
{
    cache->queries++;
    auto found = cache->entries.find(hash);
    if (found == cache->entries.end())
        return nullptr;
    cache->hits++;
    found->second.lastused = time(nullptr);
    return found->second.data;
}


/*
**  Insert a new entry, clearing out the oldest entry if the cache is
**  currently full.  The entry dropped is the oldest of those with the
**  smallest refcount.
*/
void
tdx_cache_insert(struct cache *cache, HASH hash, struct group_data *data)
{
    if (cache->entries.size() >= cache->max) {
        auto oldest = cache->entries.end();

        for (auto it = cache->entries.begin(); it != cache->entries.end();
             ++it) {
            if (oldest == cache->entries.end()) {
                oldest = it;
                continue;
            }
            if (it->second.data->refcount > oldest->second.data->refcount)
                continue;
            if (it->second.lastused > oldest->second.lastused)
                continue;
            oldest = it;
        }
        if (oldest == cache->entries.end()) {
            warn("tradindexed: unable to find oldest cache entry");
            return;
        }
        entry_release(oldest->second);
        cache->entries.erase(oldest);
    }
    if (!cache->entries.emplace(hash, cache_entry{data, time(nullptr)})
             .second) {
        warn("tradindexed: duplicate cache entry for %s", HashToText(hash));
    } else {
        data->refcount++;
    }
}


/*
**  Delete an entry from the cache.
*/
void
tdx_cache_delete(struct cache *cache, HASH hash)
{
    auto found = cache->entries.find(hash);

    if (found == cache->entries.end()) {
        warn("tradindexed: unable to remove cache entry for %s",
             HashToText(hash));
    } else {
        entry_release(found->second);
        cache->entries.erase(found);
    }
}


/*
**  Delete the cache and all of the resources that it's holding open.
*/
void
tdx_cache_free(struct cache *cache)
{
    for (auto &entry : cache->entries)
        entry_release(entry.second);
    delete cache;
}
