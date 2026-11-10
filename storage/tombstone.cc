/*
**  Helpers for the expire-tombstone log files.
**
**  See include/inn/tombstone.h for the API and inn.conf manual page
**  under "expiretombstone" for the file semantics.  Shared between
**  the expire binary and the test suite so both exercise identical
**  parsing, set, and atomic-snapshot logic.
**
**  Written by Kevin Bowling in 2026.
*/

#include "portable/system.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <unordered_set>

#include "inn/hashtab.h"
#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/storage.h"
#include "inn/tombstone.h"


/*
**  The set.  TOKENs (1 type + 1 class + 16 token bytes) are hashed and
**  compared as raw bytes: hash_lookup2 derives a stable value from the
**  bytes and memcmp gives an exact match.
*/
namespace {
struct token_hash {
    size_t
    operator()(const TOKEN &t) const
    {
        return hash_lookup2(reinterpret_cast<const char *>(&t), sizeof(TOKEN),
                            0);
    }
};

struct token_equal {
    bool
    operator()(const TOKEN &a, const TOKEN &b) const
    {
        return memcmp(&a, &b, sizeof(TOKEN)) == 0;
    }
};
} // namespace

struct tombstone_set {
    std::unordered_set<TOKEN, token_hash, token_equal> tokens;
};


struct tombstone_set *
tombstone_set_create(size_t size)
{
    auto *set = new tombstone_set;

    set->tokens.reserve(size);
    return set;
}


void
tombstone_set_free(struct tombstone_set *set)
{
    delete set;
}


bool
tombstone_add(struct tombstone_set *set, const TOKEN *token)
{
    return set->tokens.insert(*token).second;
}


size_t
tombstone_count(const struct tombstone_set *set)
{
    return set->tokens.size();
}


unsigned long
tombstone_read(struct tombstone_set *set, const char *path, bool *out_error)
{
    FILE *f;
    char line[SMBUF];
    size_t len;
    unsigned long count = 0;

    if (out_error != nullptr)
        *out_error = false;
    if (set == nullptr || path == nullptr)
        return 0;

    f = fopen(path, "r");
    if (f == nullptr) {
        if (errno != ENOENT) {
            syswarn("can't open %s; ignoring", path);
            if (out_error != nullptr)
                *out_error = true;
        }
        return 0;
    }
    while (fgets(line, sizeof(line), f) != nullptr) {
        TOKEN t;

        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0)
            continue;
        /* Comment lines (e.g., "# inn-tombstone v1") are reserved for
         * future format markers and metadata.  Readers ignore them
         * silently so format evolution is graceful. */
        if (line[0] == '#')
            continue;
        if (!IsToken(line)) {
            warn("malformed tombstone entry in %s: %s", path, line);
            continue;
        }
        t = TextToToken(line);
        /* A duplicate cancel is dropped; the existing entry stays. */
        tombstone_add(set, &t);
        count++;
    }
    if (ferror(f)) {
        syswarn("error reading %s; will use what was read", path);
        if (out_error != nullptr)
            *out_error = true;
    }
    fclose(f);
    return count;
}


char *
tombstone_rename_for_processing(const char *path)
{
    char *snapshot_path;
    int fd;

    if (path == nullptr)
        return nullptr;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        if (errno != ENOENT)
            syswarn("can't open %s", path);
        return nullptr;
    }
    if (!inn_lock_file(fd, INN_LOCK_WRITE, true)) {
        syswarn("can't lock %s", path);
        close(fd);
        return nullptr;
    }
    snapshot_path = concat(path, ".processing", (char *) nullptr);
    if (rename(path, snapshot_path) < 0) {
        syswarn("can't rename %s to %s", path, snapshot_path);
        free(snapshot_path);
        close(fd);
        return nullptr;
    }
    /* close releases the fcntl POSIX lock. */
    close(fd);
    return snapshot_path;
}


bool
tombstone_present(const struct tombstone_set *set, const TOKEN *token)
{
    if (set == nullptr || token == nullptr)
        return false;
    return set->tokens.count(*token) != 0;
}


void
tombstone_ensure_header(const char *path)
{
    static const char header[] = TOMBSTONE_HEADER;
    static const size_t header_len = sizeof(header) - 1;
    /* Bounded working memory: shift the file right by header_len bytes
     * in chunks read end-to-start.  64 KiB keeps us cache-warm without
     * paying excessive syscall overhead.  Memory is bounded regardless
     * of file size, so a pathological never-consumed file cannot OOM
     * the expire run. */
    static const size_t CHUNK = 64 * 1024;
    int fd;
    struct stat sb;
    off_t size;
    char *chunk_buf;
    off_t pos;

    if (path == nullptr)
        return;

    /* No O_APPEND: we use explicit offsets via pread/pwrite, and on
     * Linux O_APPEND would override pwrite's offset and force every
     * write to EOF -- breaking the shift loop below. */
    fd = open(path, O_RDWR | O_CREAT, 0664);
    if (fd < 0) {
        syswarn("can't open %s for header restore", path);
        return;
    }

    /* Blocking exclusive lock.  Appenders use shared locks, so we
     * conflict; the operation runs for microseconds to milliseconds
     * and only once per expire cycle, so blocking is fine.  The
     * serialization is what makes the read-modify-write safe
     * against appenders. */
    if (!inn_lock_file(fd, INN_LOCK_WRITE, true)) {
        syswarn("can't lock %s for header restore", path);
        close(fd);
        return;
    }

    if (fstat(fd, &sb) < 0) {
        syswarn("can't stat %s for header restore", path);
        close(fd);
        return;
    }
    size = sb.st_size;

    /* Idempotency fast path: if the file already starts with the
     * header line, the rest is well-formed appended content and we
     * have nothing to do.  Without this, every call would prepend an
     * additional header, growing the file by header_len each expire
     * cycle on a site with zero cancels per cycle. */
    if (size >= (off_t) header_len) {
        char prefix[sizeof(header)];

        if (pread(fd, prefix, header_len, 0) == (ssize_t) header_len
            && memcmp(prefix, header, header_len) == 0) {
            close(fd);
            return;
        }
    }

    /* Shift existing content right by header_len bytes, walking from
     * end to start so we never read a region we have already
     * overwritten.  This works in place on the same fd; memory is
     * bounded by CHUNK regardless of file size.  A crash mid-shift
     * leaves the file with some duplicated/partial lines, which
     * tombstone_read tolerates (malformed lines warn and skip); no
     * infinite escalation across crashes because the next run's
     * idempotency check sees no header and shifts once more,
     * producing a header-prefixed file the run after that leaves
     * alone. */
    chunk_buf = xmalloc(CHUNK);
    pos = size;
    while (pos > 0) {
        size_t this_chunk = (pos < (off_t) CHUNK) ? (size_t) pos : CHUNK;
        off_t src = pos - this_chunk;
        size_t io = 0;
        ssize_t n;

        while (io < this_chunk) {
            n = pread(fd, chunk_buf + io, this_chunk - io, src + io);
            if (n <= 0)
                break;
            io += n;
        }
        if (io < this_chunk) {
            syswarn("short read on %s during header restore", path);
            free(chunk_buf);
            close(fd);
            return;
        }
        io = 0;
        while (io < this_chunk) {
            n = pwrite(fd, chunk_buf + io, this_chunk - io,
                       src + header_len + io);
            if (n <= 0) {
                syswarn("short write on %s during header restore", path);
                free(chunk_buf);
                close(fd);
                return;
            }
            io += n;
        }
        pos = src;
    }
    free(chunk_buf);

    /* Header at offset 0.  Single small pwrite; partial-write on a
     * regular file is vanishingly rare but if it happens
     * tombstone_read still treats the leading '#' bytes as a
     * comment and skips them. */
    if (pwrite(fd, header, header_len, 0) != (ssize_t) header_len)
        syswarn("can't write header to %s", path);

    /* close releases the fcntl POSIX lock. */
    close(fd);
}
