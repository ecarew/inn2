/*
**  Message-ID to storage token cache.
**
**  Written by Alex Kiernan (alex.kiernan@thus.net) in 2002.
**
**  Various bug fixes, code and documentation improvements since then
**  in 2003, 2006, 2008, 2009, 2021, 2026.
**
**  Implementation of a Message-ID to storage token cache which can be
**  built during (X)OVER/(X)HDR/XPAT/NEWNEWS.  If we hit in the cache when
**  retrieving articles, the (relatively) expensive cost of a trip
**  through the history database is saved.
*/

#include "portable/system.h"

#include <list>
#include <unordered_map>

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/storage.h"

#include "cache.h"

namespace {
struct cache_entry {
    HASH hash;
    TOKEN token;
    bool used; /* Which list holds it. */
};
using entry_list = std::list<cache_entry>;

struct hash_hasher {
    size_t
    operator()(const HASH &hash) const
    {
        size_t bucket;

        memcpy(&bucket, &hash, sizeof(bucket));
        return bucket;
    }
};

struct hash_equal {
    bool
    operator()(const HASH &a, const HASH &b) const
    {
        return HashCompare(&a, &b) == 0;
    }
};

/*
**  The Message-ID to storage token cache: entries by hash, each in one of
**  two lists, oldest first.  "used" holds entries already fetched by a final
**  lookup, which are thrown away first when the cache is full; "unused"
**  holds the rest.  The cache's size, bounded by msgidcachesize, keeps
**  someone doing GROUP, (X)OVER, GROUP, (X)OVER, etc. from running out of
**  memory.
*/
std::unordered_map<HASH, entry_list::iterator, hash_hasher, hash_equal>
    msgidcache;
entry_list unused, used;


/*
**  Move an entry to the end of the unused or used list.
*/
void
cache_touch(entry_list::iterator entry, bool to_used)
{
    entry_list &from = entry->used ? used : unused;
    entry_list &to = to_used ? used : unused;

    to.splice(to.end(), from, entry);
    entry->used = to_used;
}
} // namespace


/*
**  Add a translation from HASH, h, to TOKEN, t, to the Message-ID
**  cache.
*/
void
cache_add(const HASH h, const TOKEN t)
{
    if (innconf->msgidcachesize == 0)
        return;

    auto found = msgidcache.find(h);
    if (found != msgidcache.end()) {
        cache_touch(found->second, false);
    } else {
        if (msgidcache.empty())
            msgidcache.reserve((innconf->msgidcachesize + 9) / 10);
        unused.push_back(cache_entry{h, t, false});
        msgidcache.emplace(h, std::prev(unused.end()));
    }
    if (msgidcache.size() >= innconf->msgidcachesize) {
        /* Need to throw away an entry. */
        entry_list &victims = !used.empty() ? used : unused;

        if (!victims.empty()) {
            msgidcache.erase(victims.front().hash);
            victims.pop_front();
        }
    }
}


/*
**  Lookup (and remove if found) a Message-ID to TOKEN mapping.  If this
**  is a final lookup (ARTICLE, BODY, (X)HDR, XPAT), we remove it if we
**  find it since this matches the observed behaviour of most clients, but
**  cache it just in case we can reuse it if they issue multiple
**  commands against the same Message-ID (e.g. HEAD, STAT).
*/
TOKEN
cache_get(const HASH h, bool final)
{
    static HASH last_hash;
    static TOKEN last_token;
    static const TOKEN empty_token = {TOKEN_EMPTY, 0, ""};

    if (HashCompare(&h, &last_hash) == 0 && !HashEmpty(last_hash))
        return last_token;

    auto found = msgidcache.find(h);
    if (found != msgidcache.end()) {
        cache_touch(found->second, final);
        last_hash = found->second->hash;
        last_token = found->second->token;
        return last_token;
    }
    return empty_token;
}
