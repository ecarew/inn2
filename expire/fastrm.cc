/*
**  Delete a list of filenames or tokens from stdin.
**
**  Originally written by <kre@munnari.oz.au> (to only handle files).
**
**  Files that can't be unlinked because they didn't exist are considered
**  okay.  Any error condition results in exiting with non-zero exit
**  status.  Input lines in the form @...@ are taken to be storage API
**  tokens.  Input filenames should be fully qualified.  For maximum
**  efficiency, input filenames should be sorted; fastrm will cd into each
**  directory to avoid additional directory lookups when removing a lot of
**  files in a single directory.
*/

#include "portable/system.h"

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <optional>
#include <string>
#include <string_view>
#include <syslog.h>
#include <vector>

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/newsuser.h"
#include "inn/qio.h"
#include "inn/storage.h"

/* We reject any path names longer than this. */
#define MAX_DIR_LEN 2048

/* Data structure for a list of files in a single directory. */
struct filelist {
    /* The directory part of the names.  std::nullopt when the names had no
       '/' (relative to base_dir), "" when they were directly under the root
       ("/name"): the two lead to different directories, so this is one of
       the strdup plan's std::optional cases. */
    std::optional<std::string> dir;
    std::vector<std::string> files;
};

/* All relative paths are relative to this directory. */
static char *base_dir = nullptr;

/* The absolute path of the current working directory. */
static char current_dir[MAX_DIR_LEN];

/* The prefix for the files that we're currently working with.  We sometimes
   also use this as working space for forming file names to remove, so give
   ourselves a bit of additional leeway just in case. */
static char prefix_dir[MAX_DIR_LEN * 2];
static int prefix_len;

/* Some threshold values that govern the optimizations that we are willing
   to perform.  chdir_threshold determines how many files to be removed we
   want in a directory before we chdir to that directory.  sort_threshold
   determines how many files must be in a directory before we use readdir to
   remove them in order.  relative_threshold determines how many levels of
   "../" we're willing to try to use to move to the next directory rather
   than just calling chdir with the new absolute path. */
static int chdir_threshold = 3;
static int relative_threshold = 0;
static int sort_threshold = 0;

/* True if we should only print what we would do, not actually do it. */
static bool debug_only = false;

/* A string used for constructing relative paths. */
static const char dotdots[] = "../../../../";

/* The number of errors encountered, used to determine exit status. */
static int error_count = 0;

/* Whether the storage manager has been initialized. */
static bool sm_initialized = false;

/* True if unlink may be able to remove directories. */


/*
**  Create a new filelist.
*/
static filelist *
filelist_new(std::optional<std::string> dir)
{
    return new filelist{std::move(dir), {}};
}


/*
**  Insert a file name into a list of files (unsorted).
*/
static void
filelist_insert(filelist *list, const char *name)
{
    /* Counts are passed around as int. */
    if (list->files.size() >= INT_MAX)
        die("file list grew too large");
    list->files.emplace_back(name);
}


/*
**  Find a file name in a sorted list of files.
*/
static bool
filelist_lookup(const filelist *list, std::string_view name)
{
    return std::binary_search(list->files.begin(), list->files.end(), name);
}


/*
**  Free a list of files.
*/
static void
filelist_free(filelist *list)
{
    delete list;
}


/*
**  Exit handler for die.  Shut down the storage manager before exiting.
*/
static int
sm_cleanup()
{
    SMshutdown();
    return 1;
}


/*
**  Initialize the storage manager.  This includes parsing inn.conf, which
**  fastrm doesn't need for any other purpose.
*/
static void
sm_initialize()
{
    bool value;

    if (!innconf_read(nullptr))
        exit(1);
    value = true;
    if (!SMsetrdwr(value) || !SMsetpreopen(value))
        die("can't set up storage manager");
    if (!SMinit())
        die("can't initialize storage manager: %s", SMerrorstr);
    sm_initialized = true;
    message_fatal_cleanup = sm_cleanup;
}


/*
**  Get a line from a given QIO stream, returning a pointer to it.  Warn
**  about and then skip lines that are too long.  Returns NULL at EOF or on
**  an error.
*/
static char *
get_line(QIOSTATE *qp)
{
    static int count;
    char *p;

    p = QIOread(qp);
    count++;
    while (QIOtoolong(qp) || (p != nullptr && strlen(p) >= MAX_DIR_LEN)) {
        warn("line %d too long", count);
        error_count++;
        p = QIOread(qp);
    }
    if (p == nullptr) {
        if (QIOerror(qp)) {
            syswarn("read error");
            error_count++;
        }
        return nullptr;
    }
    return p;
}


/*
**  Read lines from stdin (including the first that may have been there
**  from our last time in) until we reach EOF or until we get a line that
**  names a file not in the same directory as the previous lot.  Remember
**  the file names in the directory we're examining and return the list.
*/
static filelist *
process_line(QIOSTATE *qp, int *queued, int *deleted)
{
    static char *line = nullptr;
    filelist *list = nullptr;
    char *p;
    int dlen = -1;

    *queued = 0;
    *deleted = 0;

    if (line == nullptr)
        line = get_line(qp);

    for (; line != nullptr; line = get_line(qp)) {
        if (IsToken(line)) {
            (*deleted)++;
            if (debug_only) {
                printf("Token %s\n", line);
                continue;
            }
            if (!sm_initialized)
                sm_initialize();
            if (!SMcancel(TextToToken(line)))
                if (SMerrno != SMERR_NOENT && SMerrno != SMERR_UNINIT) {
                    warn("can't cancel %s", line);
                    error_count++;
                }
        } else {
            if (list == nullptr) {
                p = strrchr(line, '/');
                if (p != nullptr) {
                    *p++ = '\0';
                    dlen = strlen(line);
                    list = filelist_new(std::string(line, dlen));
                } else {
                    dlen = -1;
                    list = filelist_new(std::nullopt);
                }
            } else {
                if ((dlen < 0 && strchr(line, '/'))
                    || (dlen >= 0
                        && (line[dlen] != '/' || strchr(line + dlen + 1, '/')
                            || list->dir->compare(0, dlen, line, dlen) != 0)))
                    return list;
            }
            filelist_insert(list, line + dlen + 1);
            (*queued)++;
        }
    }
    return list;
}


/*
**  Copy n leading segments of a path.
*/
static void
copy_segments(char *to, const char *from, int n)
{
    char c;

    for (c = *from++; c != '\0'; c = *from++) {
        if (c == '/' && --n <= 0)
            break;
        *to++ = c;
    }
    *to = '\0';
}


/*
**  Return the count of path segments in a file name (the number of
**  slashes).
*/
static int
slashcount(char *name)
{
    int i;

    for (i = 0; *name != '\0'; name++)
        if (*name == '/')
            i++;
    return i;
}


/*
**  Unlink a file, reporting errors if the unlink fails for a reason other
**  than the file not existing doesn't exist.  fastrm refuses to run as root,
**  so unlink cannot remove a directory here.
*/
static void
unlink_file(const char *file)
{
    if (debug_only) {
        if (*file != '/')
            printf("%s / ", current_dir);
        printf("%s\n", file);
        return;
    }

    if (unlink(file) < 0 && errno != ENOENT) {
        if (*file == '/')
            syswarn("can't unlink %s", file);
        else
            syswarn("can't unlink %s in %s", file, current_dir);
    }
}


/*
**  A wrapper around chdir that dies if chdir fails for a reason other than
**  the directory not existing, returns false if the directory doesn't
**  exist (reporting an error), and otherwise returns true.  It also checks
**  to make sure that filecount is larger than chdir_threshold, and if it
**  isn't it instead just sets prefix_dir and prefix_len to point to the new
**  directory without changing the working directory.
*/
static bool
chdir_checked(const char *path, int filecount)
{
    if (filecount < chdir_threshold) {
        strlcpy(prefix_dir, path, sizeof(prefix_dir));
        prefix_len = strlen(path);
    } else {
        prefix_len = 0;
        if (chdir(path) < 0) {
            if (errno != ENOENT)
                sysdie("can't chdir from %s to %s", current_dir, path);
            else {
                syswarn("can't chdir from %s to %s", current_dir, path);
                return false;
            }
        }
    }
    return true;
}


/*
**  Set our environment (process working directory, and global vars) to
**  reflect a change of directory to dir (relative to base_dir if dir is not
**  an absolute path).  We're likely to want to do different things
**  depending on the amount of work to do in dir, so we also take the number
**  of files to remove in dir as the second argument.  Return false if the
**  directory doesn't exist (and therefore all files in it have already been
**  removed; otherwise, return true.
*/
static bool
setup_dir(const char *dir, int filecount)
{
    const char *p, *absolute;
    char *q;
    char path[MAX_DIR_LEN];
    int base_depth, depth;

    /* Set absolute to the absolute path to the new directory. */
    if (dir == nullptr)
        absolute = base_dir;
    else if (*dir == '/')
        absolute = dir;
    else if (*dir == '\0') {
        strlcpy(path, "/", sizeof(path));
        absolute = path;
    } else {
        /* Strip off leading "./". */
        while (dir[0] == '.' && dir[1] == '/')
            for (dir += 2; *dir == '/'; dir++)
                ;

        /* Handle any leading "../", but only up to the number of segments
           in base_dir. */
        base_depth = slashcount(base_dir);
        while (base_depth > 0 && strncmp(dir, "../", 3) == 0)
            for (base_depth--, dir += 3; *dir == '/'; dir++)
                ;
        if (base_depth <= 0)
            die("too many ../'s in path %s", dir);
        copy_segments(path, base_dir, base_depth + 1);
        if (strlen(path) + strlen(dir) + 2 > MAX_DIR_LEN)
            die("path %s too long", dir);
        strlcat(path, "/", sizeof(path));
        strlcat(path, dir, sizeof(path));
        absolute = path;
    }

    /* Find the first point of difference between absolute and current_dir.
       If there is no difference, we're done; we're changing to the same
       directory we were in (this is probably some sort of error, but can
       happen with odd relative paths). */
    for (p = absolute, q = current_dir; *p == *q; p++, q++)
        if (*p == '\0')
            return true;

    /* If we reached the end of current_dir and there's more left of
       absolute, we're changing to a subdirectory of where we were. */
    if (*q == '\0' && *p == '/') {
        p++;
        if (!chdir_checked(p, filecount))
            return false;
        if (prefix_len == 0)
            strlcpy(current_dir, absolute, sizeof(current_dir));
        return true;
    }

    /* Otherwise, if we were promised that we have a pure tree (in other
       words, no symbolic links to directories), see if it's worth going up
       the tree with ".." and then down again rather than chdir to the
       absolute path.  relative_threshold determines how many levels of ".."
       we're willing to use; the default of 1 seems fractionally faster than
       2 and 0 indicates to always use absolute paths.  Values larger than 3
       would require extending the dotdots string, but are unlikely to be
       worth it.

       FIXME: It's too hard to figure out what this code does.  It needs to be
       rewritten. */
    if (*p != '\0' && relative_threshold > 0) {
        depth = slashcount(q);
        if (depth <= relative_threshold) {
            while (p > absolute && *--p != '/')
                ;
            p++;
            strlcpy(prefix_dir, dotdots + 9 - depth * 3, sizeof(prefix_dir));
            strlcat(prefix_dir, p, sizeof(prefix_dir));
            if (!chdir_checked(prefix_dir, filecount))
                return false;

            /* Now patch up current_dir to reflect where we are. */
            if (prefix_len == 0) {
                while (q > current_dir && *--q != '/')
                    ;
                q[1] = '\0';
                strlcat(current_dir, p, sizeof(current_dir));
            }
            return true;
        }
    }

    /* All else has failed; just use the absolute path.  This includes the
       case where current_dir is a subdirectory of absolute, in which case
       it may be somewhat faster to use chdir("../..") or the like rather
       than the absolute path, but this case rarely happens when the user
       cares about speed (it usually doesn't happen with sorted input).  So
       we don't bother. */
    if (!chdir_checked(absolute, filecount))
        return false;
    if (prefix_len == 0)
        strlcpy(current_dir, absolute, sizeof(current_dir));
    return true;
}


/*
**  Process a filelist of files to be deleted, all in the same directory.
*/
static void
unlink_filelist(filelist *list, int filecount)
{
    bool sorted;
    DIR *dir;
    struct dirent *entry;
    char *file;
    int i;

    /* If setup_dir returns false, the directory doesn't exist and we're
       already all done. */
    if (!setup_dir(list->dir ? list->dir->c_str() : nullptr, filecount)) {
        filelist_free(list);
        return;
    }

    /* We'll use prefix_dir as a buffer to write each file name into as we
       go, so get it set up. */
    if (prefix_len == 0)
        file = prefix_dir;
    else {
        prefix_dir[prefix_len++] = '/';
        file = prefix_dir + prefix_len;
        *file = '\0';
    }

    /* If we're not sorting directories or if the number of files is under
       the threshold, just remove the files. */
    if (sort_threshold == 0 || filecount < sort_threshold) {
        for (const std::string &name : list->files) {
            strlcpy(file, name.c_str(), sizeof(prefix_dir) - prefix_len);
            unlink_file(prefix_dir);
        }
        filelist_free(list);
        return;
    }

    /* We have enough files to remove in this directory that it's worth
       optimizing.  First, make sure the list of files is sorted.  It's not
       uncommon for the files to already be sorted, so check first. */
    sorted = std::is_sorted(list->files.begin(), list->files.end());
    if (!sorted)
        std::sort(list->files.begin(), list->files.end());

    /* Now, begin doing our optimized unlinks.  The technique we use is to
       open the directory containing the files and read through it, checking
       each file in the directory to see if it's one of the files we should
       be removing.  The theory is that we want to minimize the amount of
       time the operating system spends doing string compares trying to find
       the file to be removed in the directory.  This is often an O(n)
       operation.  Note that this optimization may slightly slow more
       efficient operating systems. */
    dir = opendir(prefix_len == 0 ? "." : prefix_dir);
    if (dir == nullptr) {
        if (prefix_len > 0 && prefix_dir[0] == '/')
            warn("can't open directory %s", prefix_dir);
        else
            warn("can't open directory %s in %s",
                 (prefix_len == 0) ? "." : prefix_dir, current_dir);
        error_count++;
        filelist_free(list);
        return;
    }
    for (i = 0, entry = readdir(dir); entry != nullptr; entry = readdir(dir))
        if (filelist_lookup(list, entry->d_name)) {
            i++;
            strlcpy(file, entry->d_name, sizeof(prefix_dir) - prefix_len);
            unlink_file(prefix_dir);
            if ((size_t) i == list->files.size())
                break;
        }
    closedir(dir);
    filelist_free(list);
}


/*
**  Check a path to see if it's okay (not likely to confuse us).  This
**  ensures that it doesn't contain elements like "./" or "../" and doesn't
**  contain doubled slashes.
*/
static bool
bad_path(const char *p)
{
    if (strlen(p) >= MAX_DIR_LEN)
        return true;
    while (*p) {
        if (p[0] == '.' && (p[1] == '/' || (p[1] == '.' && p[2] == '/')))
            return true;
        while (*p && *p != '/')
            p++;
        if (p[0] == '/' && p[1] == '/')
            return true;
        if (*p == '/')
            p++;
    }
    return false;
}


/*
**  Main routine.  Parse options, initialize the storage manager, and
**  initialize various global variables, and then go into a loop calling
**  process_line and unlink_filelist as needed.
*/
int
main(int argc, char *argv[])
{
    const char *name;
    const char *p;
    char **arg;
    QIOSTATE *qp;
    filelist *list;
    int filecount, deleted;
    bool empty_error = false;

    /* Establish our identity.  Since we use the storage manager, we need to
       set up syslog as well, although we won't use it ourselves. */
    name = argv[0];
    if (*name == '\0')
        name = "fastrm";
    else {
        p = strrchr(name, '/');
        if (p != nullptr)
            name = p + 1;
    }
    message_program_name = name;
    ensure_not_root();
    openlog(name, LOG_CONS | LOG_PID, LOG_INN_PROG);

    /* Unfortunately, we can't use getopt, because several of our options
       take optional arguments.  Bleh. */
    arg = argv + 1;
    while (argc >= 2 && **arg == '-') {
        p = *arg;
        while (*++p) {
            switch (*p) {
            default:
                die("invalid option -- %c", *p);
            case 'a':
            case 'r':
                continue;
            case 'c':
                chdir_threshold = 1;
                if (!isdigit((unsigned char) p[1]))
                    continue;
                chdir_threshold = atoi(p + 1);
                break;
            case 'd':
                debug_only = true;
                continue;
            case 'e':
                empty_error = true;
                continue;
            case 's':
                sort_threshold = 5;
                if (!isdigit((unsigned char) p[1]))
                    continue;
                sort_threshold = atoi(p + 1);
                break;
            case 'u':
                relative_threshold = 1;
                if (!isdigit((unsigned char) p[1]))
                    continue;
                relative_threshold = atoi(p + 1);
                if (relative_threshold >= (int) strlen(dotdots) / 3)
                    relative_threshold = strlen(dotdots) / 3 - 1;
                break;
            }
            break;
        }
        argc--;
        arg++;
    }
    if (argc != 2)
        die("usage error, wrong number of arguments");

    /* The remaining argument is the base path.  Make sure it's valid and
       not excessively large and then change to it. */
    base_dir = *arg;
    if (*base_dir != '/' || bad_path(base_dir))
        die("bad base path %s", base_dir);
    strlcpy(current_dir, base_dir, sizeof(current_dir));
    if (chdir(current_dir) < 0)
        sysdie("can't chdir to base path %s", current_dir);

    /* Open our input stream and then loop through it, building filelists
       and processing them until done. */
    qp = QIOfdopen(fileno(stdin));
    if (qp == nullptr)
        sysdie("can't reopen stdin");
    while ((list = process_line(qp, &filecount, &deleted)) != nullptr) {
        empty_error = false;
        unlink_filelist(list, filecount);
    }
    if (deleted > 0)
        empty_error = false;

    /* All done. */
    SMshutdown();
    if (empty_error)
        die("no files to remove");
    exit(error_count > 0 ? 1 : 0);
}
