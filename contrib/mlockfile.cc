/* Locks the files given on the command line into memory using mlock.
   This code has only been tested on Solaris and may not work on other
   platforms.

   Contributed by Alex Kiernan <alexk@demon.net>. */

#include "portable/system.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#include <limits>
#include <string>
#include <vector>

/* One file (or part of it) to keep in memory. */
struct Locked {
    std::string path;
    struct stat st = {}; /* of the file as it was last locked */
    void *base = MAP_FAILED;
    off_t offset = 0;
    size_t length = 0; /* 0: the whole file */

    Locked() = default;
    Locked(const Locked &) = delete;
    Locked &operator=(const Locked &) = delete;
    Locked(Locked &&) = default;
    ~Locked() { unmap(); }

    size_t size_for(const struct stat &s) const
    {
        return length > 0 ? length : static_cast<size_t>(s.st_size);
    }

    void unmap()
    {
        if (base != MAP_FAILED)
            munmap(base, size_for(st));
        base = MAP_FAILED;
        st = {};
    }
};

/* Closes the descriptor when it goes out of scope. */
struct Fd {
    int fd;
    explicit Fd(int f) : fd(f) {}
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    ~Fd()
    {
        if (fd != -1)
            close(fd);
    }
};

static const char *progname;
static bool flush = false;
static bool once = false;
static int interval = 60000;

/* Lock, or refresh, every file.  A file whose size, inode or device changed
   is unlocked and the new one locked.  Returns false if any file could not
   be locked. */
static bool
inn_lock_files(std::vector<Locked> &files)
{
    bool ok = true;

    for (Locked &ml : files) {
        Fd f(open(ml.path.c_str(), O_RDONLY));
        if (f.fd == -1) {
            fprintf(stderr, "%s: can't open `%s' - %s\n", progname,
                    ml.path.c_str(), strerror(errno));
            ok = false;
            continue;
        }

        struct stat st;
        if (fstat(f.fd, &st) != 0) {
            fprintf(stderr, "%s: can't stat `%s' - %s\n", progname,
                    ml.path.c_str(), strerror(errno));
            ok = false;
        } else if (ml.st.st_ino != st.st_ino || ml.st.st_dev != st.st_dev
                   || ml.st.st_size != st.st_size) {
            /* Free everything here, so in case of failure we try again next
             * time. */
            ml.unmap();

            ml.base = mmap(NULL, ml.size_for(st), PROT_READ, MAP_SHARED, f.fd,
                           ml.offset);
            if (ml.base == MAP_FAILED) {
                fprintf(stderr, "%s: can't mmap `%s' - %s\n", progname,
                        ml.path.c_str(), strerror(errno));
                ok = false;
            } else if (mlock(ml.base, ml.size_for(st)) != 0) {
                fprintf(stderr, "%s: can't mlock `%s' - %s\n", progname,
                        ml.path.c_str(), strerror(errno));
                munmap(ml.base, ml.size_for(st));
                ml.base = MAP_FAILED;
                ok = false;
            } else {
                ml.st = st;
            }
        } else if (flush) {
            msync(ml.base, ml.size_for(st), MS_SYNC);
        }
    }
    return ok;
}

static void
usage(void)
{
    fprintf(stderr,
            "usage: %s [-1f] [-i interval] file[@offset[:length]] ...\n",
            progname);
    fprintf(stderr, "    -1\tlock the files once and exit with the result\n");
    fprintf(stderr, "    -f\tflush locked bitmaps at interval\n");
    fprintf(stderr,
            "    -i interval\n\tset interval between checks/flushes\n");
}

/* Parse an unsigned number ending at `stop` (a character, or '\0').  Returns
   false on a syntax error or overflow, and leaves *end after the number. */
static bool
parse_number(const char *text, unsigned long long *value, char **end)
{
    if (*text == '-' || *text == '+' || *text == '\0')
        return false;
    errno = 0;
    *value = strtoull(text, end, 0);
    return errno == 0 && *end != text;
}

/* Parse file[@offset[:length]] into ml.  Prints the error and returns false
   if it is malformed. */
static bool
parse_spec(const char *spec, Locked &ml)
{
    const char *at = strchr(spec, '@');

    ml.path.assign(spec, at != NULL ? static_cast<size_t>(at - spec)
                                    : strlen(spec));
    if (at == NULL)
        return true;

    char *end;
    unsigned long long value;
    ++at;
    if (!parse_number(at, &value, &end)
        || value > static_cast<unsigned long long>(
               std::numeric_limits<off_t>::max())) {
        fprintf(stderr, "%s: can't parse offset `%s'\n", progname, at);
        return false;
    }
    ml.offset = static_cast<off_t>(value);

    if (*end == ':') {
        at = end + 1;
        if (!parse_number(at, &value, &end)
            || value > std::numeric_limits<size_t>::max()) {
            fprintf(stderr, "%s: can't parse length `%s'\n", progname, at);
            return false;
        }
        ml.length = static_cast<size_t>(value);
    }
    if (*end != '\0') {
        fprintf(stderr, "%s: unrecognised separator `%c'\n", progname, *end);
        return false;
    }
    return true;
}

int
main(int argc, char *argv[])
{
    int i;

    progname = *argv;
    while ((i = getopt(argc, argv, "1fi:")) != EOF) {
        switch (i) {
        case '1':
            once = true;
            break;

        case 'i': {
            char *end;
            long seconds = strtol(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || seconds < 1
                || seconds > 2000000) {
                fprintf(stderr, "%s: bad interval `%s'\n", progname, optarg);
                return EX_USAGE;
            }
            interval = 1000 * static_cast<int>(seconds);
            break;
        }

        case 'f':
            flush = true;
            break;

        default:
            usage();
            return EX_USAGE;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0) {
        usage();
        return EX_USAGE;
    }

    /* Construct the list of files to operate on; nothing is locked until
     * the first pass. */
    std::vector<Locked> files(argc);
    for (i = 0; i < argc; i++)
        if (!parse_spec(argv[i], files[i]))
            return EX_USAGE;

    /* Loop over the list of files, sleeping between iterations. */
    for (;;) {
        bool ok = inn_lock_files(files);
        if (once)
            return ok ? 0 : EX_OSERR;
        poll(NULL, 0, interval);
    }
}
