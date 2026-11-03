/*
 * EXPIRECTL.C
 *
 * expirectl
 *
 * This program uses expire.ctl.ctl as input; please see the end of this
 * file for an example of such a file.
 *
 * Various bug fixes, code and documentation improvements
 * in 2001, 2011, 2014, 2015, 2021, 2024, 2026.
 */

/*
 * Date: Mon, 21 Nov 1994 12:29:52 -0801
 * From: Matthew Dillon <dillon@apollo.west.oic.com>
 * Message-Id: <199411212030.MAA21835@apollo.west.oic.com>
 * To: rsalz@uunet.uu.net
 * Subject: Re:  INN is great, bug fix for BSDI
 *
 * [...]
 *     Oh, while I'm at it, I also wrote a cute program that builds the
 *     expire.ctl file dynamically based on available space.  Feel free
 *     to include this in the dist (or not) as you please.
 *
 *     Basically, the expirectl programs determines the amount of disk blocks
 *     and inodes free in the spool and creates a new expire.ctl file based
 *     on an expire.ctl.ctl template.  The template specifies expiration times
 *     as a fraction of nominal.  expirectl adjusts the nominal expiration
 *     up or down based on available disk space.
 *
 *     The idea is to make expiration as hands off as possible.  I tested
 *     it on a smaller spool and it appeared to work fine.  Currently it
 *     only works for single-partition news spools tho.  The above spool
 *     will not really exercise the program for another 14 days or so :-).
 */

#include "portable/system.h"

#include <errno.h>
#include <time.h>

#include <string>

/* Following portability code lifted from inndf.c */
#if HAVE_STATVFS
#    include <sys/statvfs.h>
#    define df_stat(p, s) (statvfs((p), (s)) == 0)
#    define df_declare(s) struct statvfs s
#    define df_avail(s)   ((s).f_bavail)
#    define df_scale(s)   ((s).f_frsize == 0 ? (s).f_bsize : (s).f_frsize)
#    define df_favail(s)  ((s).f_favail)
#elif HAVE_STATFS
#    if HAVE_SYS_VFS_H
#        include <sys/vfs.h>
#    endif
#    if HAVE_SYS_PARAM_H
#        include <sys/param.h>
#    endif
#    if HAVE_SYS_MOUNT_H
#        include <sys/mount.h>
#    endif
#    ifdef __ultrix__
#        define df_stat(p, s) (statfs((p), (s)) >= 1)
#        define df_declare(s) struct fs_data s
#        define df_avail(s)   ((s).fd_bfreen)
#        define df_scale(s)   1024
#        define df_favail(s)  ((s).fd_gfree)
#    else
#        define df_stat(p, s) (statfs((p), (s)) == 0)
#        define df_declare(s) struct statfs s
#        define df_avail(s)   ((s).f_bavail)
#        define df_scale(s)   ((s).f_bsize)
#        define df_favail(s)  ((s).f_ffree)
#    endif
#else
#    error "Platform not supported.  Neither statvfs nor statfs available."
#endif

/* Pick the longest available integer type. */
#if HAVE_LONG_LONG_INT
typedef unsigned long long long_int_type;
#else
typedef unsigned long long_int_type;
#endif

/* The directory expirectl works in, unless -d gives another one. */
#define EXPIRE_CTL_DIR "/home/news"

/* Parse a number with an optional k or m suffix (kilo- or mega-).  Returns
   false if there is anything else after the number. */
static bool
parse_size(const char *text, long *result)
{
    char *end;

    errno = 0;
    long value = strtol(text, &end, 0);
    if (end == text || errno != 0)
        return false;
    if (*end == 'k') {
        value *= 1024;
        end++;
    } else if (*end == 'm') {
        value *= 1024 * 1024;
        end++;
    }
    if (*end != '\0')
        return false;
    *result = value;
    return true;
}

/* Replace each [N] or [N/minimum] in a line of the template with N times the
   number of days (at least minimum, and at least 1). */
static std::string
expand_line(const std::string &line, long expireDays)
{
    std::string out;
    size_t pos = 0;

    for (;;) {
        size_t open = line.find('[', pos);
        if (open == std::string::npos)
            break;
        out.append(line, pos, open - pos);

        const char *start = line.c_str() + open + 1;
        char *end;
        double d = strtod(start, &end);
        long m = 0;
        if (*end == '/')
            m = strtol(end + 1, &end, 0);
        if (*end == ']') {
            long v = (long) ((double) expireDays * d + 0.5);
            if (v < 1)
                v = 1;
            if (v < m)
                v = m;
            out += std::to_string(v);
            ++end;
        }
        /* If it was not a valid [N] the text after the bracket is kept. */
        pos = end - line.c_str();
    }
    out.append(line, pos, std::string::npos);
    return out;
}

/* Write the control file from its template.  Returns false if the template
   could not be used. */
static bool
write_expire_ctl(const std::string &dir, long expireDays)
{
    std::string ctl = dir + "/expire.ctl";
    std::string tmp = ctl + ".tmp";
    std::string tmpl = ctl + ".ctl";

    FILE *fi = fopen(tmpl.c_str(), "r");
    if (fi == NULL) {
        fprintf(stderr, "expirectl: unable to open %s: %s\n", tmpl.c_str(),
                strerror(errno));
        return false;
    }
    FILE *fo = fopen(tmp.c_str(), "w");
    if (fo == NULL) {
        fprintf(stderr, "expirectl: unable to create %s: %s\n", tmp.c_str(),
                strerror(errno));
        fclose(fi);
        return false;
    }

    /* Lines of any length, unlike a fixed buffer. */
    std::string line;
    char chunk[512];
    bool ok = true;
    while (fgets(chunk, sizeof(chunk), fi) != NULL) {
        line += chunk;
        if (line.back() != '\n' && !feof(fi))
            continue;
        std::string expanded = expand_line(line, expireDays);
        if (fputs(expanded.c_str(), fo) == EOF)
            ok = false;
        line.clear();
    }
    if (fclose(fo) != 0)
        ok = false;
    fclose(fi);

    if (!ok) {
        fprintf(stderr, "expirectl: error writing %s\n", tmp.c_str());
        unlink(tmp.c_str());
        return false;
    }
    if (rename(tmp.c_str(), ctl.c_str()) != 0) {
        fprintf(stderr, "rename(%s,%s): %s\n", tmp.c_str(), ctl.c_str(),
                strerror(errno));
        return false;
    }
    return true;
}

int
main(int ac, char **av)
{
    df_declare(sfs);
    long minFree = 100 * 1024 * 1024;
    long minIFreeSigned = 20 * 1024;
    long expireDays = 2;
    time_t expireIncTime = time(NULL) - 24 * 60 * 60;
    int modified = 0;
    int verbose = 0;
    std::string dir = EXPIRE_CTL_DIR;

    /*
     * options
     */

    for (int i = 1; i < ac; ++i) {
        char *ptr = av[i];

        if (*ptr != '-' || ptr[1] == '\0') {
            fprintf(stderr, "bad option: %s\n", ptr);
            exit(1);
        }
        char opt = ptr[1];
        ptr += 2;
        switch (opt) {
        case 'v':
            verbose = 1;
            break;
        case 'f':
            modified = 1;
            break;
        case 'n':
            modified = -1;
            break;
        case 'b':
        case 'i':
        case 'd': {
            /* The argument is attached (-b100) or the next word. */
            if (*ptr == '\0') {
                if (++i >= ac) {
                    fprintf(stderr, "option -%c needs an argument\n", opt);
                    exit(1);
                }
                ptr = av[i];
            }
            if (opt == 'd') {
                dir = ptr;
            } else if (!parse_size(ptr, opt == 'b' ? &minFree
                                                    : &minIFreeSigned)) {
                fprintf(stderr, "bad value for -%c: %s\n", opt, ptr);
                exit(1);
            }
            break;
        }
        default:
            fprintf(stderr, "bad option: %s\n", av[i]);
            exit(1);
        }
    }
    long_int_type minIFree = minIFreeSigned < 0 ? 0 : minIFreeSigned;

    std::string spool = dir + "/spool/news/.";
    std::string daysFile = dir + "/expire.days";

    if (!df_stat(spool.c_str(), &sfs)) {
        fprintf(stderr, "expirectl: couldn't fsstat %s\n", spool.c_str());
        exit(1);
    }

    /*
     * Load expire.days
     */

    {
        char buf[256];

        FILE *fi = fopen(daysFile.c_str(), "r");
        if (fi != NULL) {
            while (fgets(buf, sizeof(buf), fi) != NULL) {
                if (strncmp(buf, "time", 4) == 0) {
                    expireIncTime = strtoul(buf + 4, NULL, 0);
                } else if (strncmp(buf, "days", 4) == 0) {
                    expireDays = strtol(buf + 4, NULL, 0);
                }
            }
            fclose(fi);
        } else {
            if (modified >= 0)
                modified = 1;
            printf("creating %s\n", daysFile.c_str());
        }
    }

    /*
     * print status
     */

    if (verbose) {
        printf("spool: %4.2lfM / %3.2lfKinode free\n",
               (double) df_scale(sfs) * (double) df_avail(sfs)
                   / (1024.0 * 1024.0),
               (double) df_favail(sfs) / 1024.0);
        printf("decrs: %4.2lfM / %3.2lfKinode\n",
               (double) (minFree) / (double) (1024 * 1024),
               (double) (minIFree) / (double) (1024));
        printf("incrs: %4.2lfM / %3.2lfKinode\n",
               (double) (minFree * 2) / (double) (1024 * 1024),
               (double) (minIFree * 2) / (double) (1024));
    }

    /*
     * Check limits, update as appropriate
     */

    {
        double bytes = (double) df_scale(sfs) * (double) df_avail(sfs);
        long_int_type inodes = df_favail(sfs);

        if (bytes < (double) minFree || inodes < minIFree) {
            if (--expireDays <= 0) {
                expireDays = 1;
                expireIncTime = time(NULL) - 24 * 60 * 60;
            }
            if (modified >= 0)
                modified = 1;
            printf("decrement expiration to %ld days\n", expireDays);
        } else if (bytes >= (double) minFree * 2.0 && inodes >= minIFree * 2) {
            long dt = (long) (time(NULL) - expireIncTime);

            if (dt >= 60 * 60 * 24 || dt < -60) {
                ++expireDays;
                expireIncTime = time(NULL);
                if (modified >= 0)
                    modified = 1;
                printf("increment expiration to %ld days\n", expireDays);
            } else {
                printf("will increment expiration later\n");
            }
        } else if (verbose) {
            printf("expiration unchanged: %ld\n", expireDays);
        }
    }

    /*
     * Write expire.ctl from the expire.ctl.ctl template, then expire.days.
     * If the template cannot be used the days file is left alone, so the
     * next run starts from the same state.
     */

    int status = 0;
    if (modified > 0) {
        if (!write_expire_ctl(dir, expireDays)) {
            status = 1;
        } else {
            FILE *fo = fopen(daysFile.c_str(), "w");
            if (fo != NULL) {
                fprintf(fo, "time 0x%08lx\n", (unsigned long) expireIncTime);
                fprintf(fo, "days %ld\n", expireDays);
                fclose(fo);
            } else {
                fprintf(stderr, "unable to create %s\n", daysFile.c_str());
                status = 1;
            }
        }
    }
    exit(status);
}


/*

# Start of sample expire.ctl.ctl file.

# EXPIRE.CTL.CTL (EXPIRE.CTL GENERATED FROM EXPIRE.CTL.CTL !!!)
#
# The expire.ctl file is generated by the expirectl program from the
# expire.ctl.ctl file.  The expirectl program calculates the proper
# expiration based on the number of free inodes and free bytes available.
#
# This file is exactly expire.ctl but with the multiplier [N] replaced by
# a calculated value, where a multiplier of '1' nominally fills the whole
# disk.
#
# Any field [N] is substituted after being multiplied by the expiration
# time (in days).  A integer minimum can also be specified with a slash,
# as in [N/minimum].
#
# expirectl is normally run just after expire is run.  Note that expirectl
# isn't very useful for the case where you are 'catching up' on news after
# a long period of downtime UNLESS you use the -p option to expire.

/remember/:[1.2/20]

##  Keep for 1-10 days, allow Expires header fields to work.
#
*:A:1:[1.0]:[6.0]
*.advocacy:A:1:[0.5]:[2.0]
alt.binaries.pictures.erotica:A:1:[0.8]:[2.0]

# permanent, semi-permanent
#
best.intro:A:never:never:never
best.announce:A:5:60:120
best.general:A:never:never:never
best.bugs:A:never:never:never

# End of sample expire.ctl.ctl file.

*/
