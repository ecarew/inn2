/*
**  wildbench: correctness cases and timing for nativemat, limited to the
**  fields INN actually matches.
**
**  Fields in scope (and where they are matched):
**    newsgroup names   readers.conf read:/post: through PERMmatch (LIST
**                      ACTIVE, GROUP, NEWNEWS), newsfeeds, storage.conf,
**                      LIST ACTIVE <wildmat> from the client
**    host names, IPs   readers.conf hosts: (nnrpd/perm.cc)
**    user names        readers.conf users:
**    distributions     newsfeeds /dist, nnrpd post checks
**    header text       XPAT and HDR patterns (nativemat_simple)
**
**  Usage:
**    wildbench check [-l ms] [-v] FILE...
**        Runs every case in the case files.  Fails on a wrong answer or on
**        a case slower than the limit (default 50 ms).
**    wildbench active [-r reps] ACTIVE LISTS
**        For each access list in LISTS, runs the PERMmatch decision over
**        every group in ACTIVE (first field of each line), scanning the
**        list forward (as INN 2.8 did) and in reverse (stopping at the
**        first match), and prints for each the groups accepted, a hash of
**        the decisions and the time per group.  Two variants agree only if
**        every hash agrees.
**
**  Case files: one case per line, tab-separated, '#' starts a comment.
**    tier  func  expect  text  pattern  [note]
**  tier is easy, complex, obscure or hostile; func is m (nativemat), s
**  (nativemat_simple) or p (nativemat_poison); expect is 0 or 1 for m and s,
**  and F, M or P (fail, match, poison) for p.  In text and pattern, \t, \\
**  and \xNN are escapes, and $(N:str) is str repeated N times, for the
**  long inputs of the hostile tier.
**
**  Lists file: one list per line, "name<TAB>pattern,pattern,...", split on
**  commas as readers.conf does.
**
**  The cases and lists are in support/perf/wildmat/.  The group list is
**  ISC's active file, fetched when run rather than kept in the tree:
**    https://ftp.isc.org/pub/usenet/CONFIG/active.bz2
**
**  Build against a configured tree:
**    cc -std=c11 -O2 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Iinclude \
**       -Ibuild/include support/perf/wildbench.c build/lib/libinn.a \
**       -o wildbench
*/

#include "portable/system.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "inn/libinn.h"

static double
now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void *
xalloc(size_t n)
{
    void *p = malloc(n);

    if (p == NULL) {
        fprintf(stderr, "wildbench: out of memory\n");
        exit(2);
    }
    return p;
}

/* Growable byte buffer for expanded fields. */
struct buf {
    char *data;
    size_t len, size;
};

static void
buf_add(struct buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->size) {
        size_t size = b->size ? b->size : 64;

        while (b->len + n + 1 > size)
            size *= 2;
        b->data = realloc(b->data, size);
        if (b->data == NULL) {
            fprintf(stderr, "wildbench: out of memory\n");
            exit(2);
        }
        b->size = size;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

/*
**  Expand escapes and $(N:str) repeats.  Returns NULL on a malformed field
**  (or one that would contain a NUL, which no field INN matches can hold).
*/
static char *
expand(const char *in)
{
    struct buf b = {NULL, 0, 0};
    const char *p;

    buf_add(&b, "", 0);
    for (p = in; *p != '\0'; p++) {
        if (p[0] == '$' && p[1] == '(') {
            char *colon, *close, *inner;
            unsigned long n;

            n = strtoul(p + 2, &colon, 10);
            close = colon == NULL ? NULL : strchr(colon, ')');
            if (*colon != ':' || close == NULL) {
                free(b.data);
                return NULL;
            }
            inner = xalloc(close - colon);
            memcpy(inner, colon + 1, close - colon - 1);
            inner[close - colon - 1] = '\0';
            {
                char *piece = expand(inner);
                size_t len;

                free(inner);
                if (piece == NULL) {
                    free(b.data);
                    return NULL;
                }
                len = strlen(piece);
                while (n-- > 0)
                    buf_add(&b, piece, len);
                free(piece);
            }
            p = close;
        } else if (p[0] == '\\' && p[1] == 't') {
            buf_add(&b, "\t", 1);
            p++;
        } else if (p[0] == '\\' && p[1] == '\\') {
            buf_add(&b, "\\", 1);
            p++;
        } else if (p[0] == '\\' && p[1] == 'x' && isxdigit((unsigned char) p[2])
                   && isxdigit((unsigned char) p[3])) {
            char hex[3] = {p[2], p[3], '\0'};
            char c = (char) strtoul(hex, NULL, 16);

            if (c == '\0') {
                free(b.data);
                return NULL;
            }
            buf_add(&b, &c, 1);
            p += 3;
        } else {
            buf_add(&b, p, 1);
        }
    }
    return b.data;
}

/* Split line on tabs in place; returns the number of fields. */
static int
split_tabs(char *line, char **fields, int max)
{
    int n = 0;
    char *p = line;

    while (n < max) {
        fields[n++] = p;
        p = strchr(p, '\t');
        if (p == NULL)
            break;
        *p++ = '\0';
    }
    return n;
}

static char *
read_line(FILE *f)
{
    struct buf b = {NULL, 0, 0};
    char chunk[4096];

    while (fgets(chunk, sizeof(chunk), f) != NULL) {
        buf_add(&b, chunk, strlen(chunk));
        if (b.len > 0 && b.data[b.len - 1] == '\n')
            break;
    }
    if (b.data == NULL)
        return NULL;
    while (b.len > 0 && (b.data[b.len - 1] == '\n' || b.data[b.len - 1] == '\r'))
        b.data[--b.len] = '\0';
    return b.data;
}

static const char *
show(const char *s)
{
    static char out[200];
    size_t i, o = 0;

    for (i = 0; s[i] != '\0' && o < sizeof(out) - 12; i++) {
        unsigned char c = (unsigned char) s[i];

        if (c < 0x20 || c >= 0x7f || c == '\\')
            o += snprintf(out + o, sizeof(out) - o, "\\x%02x", c);
        else
            out[o++] = (char) c;
    }
    if (s[i] != '\0')
        o += snprintf(out + o, sizeof(out) - o, "...(%zu)", strlen(s));
    out[o] = '\0';
    return out;
}

static int
run_check(int argc, char **argv)
{
    double limit = 0.050;
    int verbose = 0, cases = 0, failures = 0, i;
    double slowest = 0;
    char slowest_where[256] = "";

    for (i = 0; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-v") == 0)
            verbose = 1;
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
            limit = atof(argv[++i]) / 1000.0;
        else {
            fprintf(stderr, "wildbench: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (i == argc) {
        fprintf(stderr, "wildbench check: no case files\n");
        return 2;
    }
    for (; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        char *line;
        int lineno = 0;

        if (f == NULL) {
            fprintf(stderr, "wildbench: %s: %s\n", argv[i], strerror(errno));
            return 2;
        }
        while ((line = read_line(f)) != NULL) {
            char *fld[6];
            int n;

            lineno++;
            if (line[0] == '#' || line[0] == '\0') {
                free(line);
                continue;
            }
            n = split_tabs(line, fld, 6);
            if (n < 5 || strlen(fld[1]) != 1 || strlen(fld[2]) != 1) {
                fprintf(stderr, "%s:%d: malformed case\n", argv[i], lineno);
                failures++;
            } else {
                char *text = expand(fld[3]), *pat = expand(fld[4]);
                char func = fld[1][0], want = fld[2][0], got = '?';
                double start, took;

                if (text == NULL || pat == NULL) {
                    fprintf(stderr, "%s:%d: malformed field\n", argv[i],
                            lineno);
                    failures++;
                    free(text);
                    free(pat);
                    free(line);
                    continue;
                }
                start = now();
                switch (func) {
                case 'm':
                    got = nativemat(text, pat) ? '1' : '0';
                    break;
                case 's':
                    got = nativemat_simple(text, pat) ? '1' : '0';
                    break;
                case 'p':
                    switch (nativemat_poison(text, pat)) {
                    case NATIVEMAT_FAIL:
                        got = 'F';
                        break;
                    case NATIVEMAT_MATCH:
                        got = 'M';
                        break;
                    case NATIVEMAT_POISON:
                        got = 'P';
                        break;
                    }
                    break;
                default:
                    fprintf(stderr, "%s:%d: unknown func %c\n", argv[i],
                            lineno, func);
                    failures++;
                    break;
                }
                took = now() - start;
                cases++;
                if (took > slowest) {
                    slowest = took;
                    snprintf(slowest_where, sizeof(slowest_where), "%s:%d",
                             argv[i], lineno);
                }
                if (got != want || took > limit) {
                    failures++;
                    printf("FAIL %s:%d [%s] %c want %c got %c %.3f ms%s\n",
                           argv[i], lineno, fld[0], func, want, got,
                           took * 1000, took > limit ? " (too slow)" : "");
                    printf("  text    %s\n", show(text));
                    printf("  pattern %s\n", show(pat));
                } else if (verbose) {
                    printf("ok   %s:%d [%s] %c %c %.3f ms\n", argv[i], lineno,
                           fld[0], func, got, took * 1000);
                }
                free(text);
                free(pat);
            }
            free(line);
        }
        fclose(f);
    }
    printf("cases %d failures %d slowest %.3f ms at %s\n", cases, failures,
           slowest * 1000, slowest_where);
    return failures == 0 ? 0 : 1;
}

/*
**  The PERMmatch decision for one group, as nnrpd/misc.cc made it through
**  INN 2.8: every pattern is tried and the last one that matches decides.
*/
static bool
permmatch_forward(char **pats, const char *group)
{
    bool match = false;
    int i;

    if (pats == NULL || pats[0] == NULL)
        return true;
    for (i = 0; pats[i] != NULL; i++) {
        if (pats[i][0] == '!') {
            if (nativemat(group, pats[i] + 1))
                match = false;
        } else if (nativemat(group, pats[i]))
            match = true;
    }
    return match;
}

/*
**  The same decision scanning from the last pattern and stopping at the
**  first that matches, which decides.
*/
static bool
permmatch_reverse(char **pats, const char *group)
{
    size_t n = 0;

    if (pats == NULL || pats[0] == NULL)
        return true;
    while (pats[n] != NULL)
        n++;
    while (n-- > 0) {
        bool negated = pats[n][0] == '!';

        if (nativemat(group, negated ? pats[n] + 1 : pats[n]))
            return !negated;
    }
    return false;
}

static const struct {
    const char *name;
    bool (*decide)(char **, const char *);
} variants[] = {
    {"forward", permmatch_forward},
    {"reverse", permmatch_reverse},
};

struct strings {
    char **v;
    size_t n, size;
};

static void
strings_add(struct strings *s, char *str)
{
    if (s->n + 2 > s->size) {
        s->size = s->size ? s->size * 2 : 1024;
        s->v = realloc(s->v, s->size * sizeof(char *));
        if (s->v == NULL) {
            fprintf(stderr, "wildbench: out of memory\n");
            exit(2);
        }
    }
    s->v[s->n++] = str;
    s->v[s->n] = NULL;
}

static int
run_active(int argc, char **argv)
{
    long reps = 5;
    struct strings groups = {NULL, 0, 0};
    FILE *f;
    char *line;
    int i = 0;

    if (argc >= 2 && strcmp(argv[0], "-r") == 0) {
        reps = atol(argv[1]);
        i = 2;
    }
    if (argc - i != 2 || reps < 1) {
        fprintf(stderr, "usage: wildbench active [-r reps] ACTIVE LISTS\n");
        return 2;
    }
    f = fopen(argv[i], "r");
    if (f == NULL) {
        fprintf(stderr, "wildbench: %s: %s\n", argv[i], strerror(errno));
        return 2;
    }
    while ((line = read_line(f)) != NULL) {
        char *end = line + strcspn(line, " \t");

        *end = '\0';
        if (line[0] != '\0')
            strings_add(&groups, line);
        else
            free(line);
    }
    fclose(f);
    printf("groups %zu\n", groups.n);

    f = fopen(argv[i + 1], "r");
    if (f == NULL) {
        fprintf(stderr, "wildbench: %s: %s\n", argv[i + 1], strerror(errno));
        return 2;
    }
    while ((line = read_line(f)) != NULL) {
        struct strings pats = {NULL, 0, 0};
        char *fld[2], *p, *tok;
        size_t v;

        if (line[0] == '#' || line[0] == '\0'
            || split_tabs(line, fld, 2) != 2) {
            free(line);
            continue;
        }
        for (p = fld[1]; (tok = strsep(&p, ",")) != NULL;)
            if (*tok != '\0')
                strings_add(&pats, tok);

        for (v = 0; v < sizeof(variants) / sizeof(variants[0]); v++) {
            uint64_t hash = 14695981039346656037ULL;
            size_t accepted = 0, g;
            double best = 0;
            long r;

            for (r = 0; r < reps; r++) {
                double start = now(), took;

                accepted = 0;
                hash = 14695981039346656037ULL;
                for (g = 0; g < groups.n; g++) {
                    bool ok = variants[v].decide(pats.v, groups.v[g]);

                    accepted += ok;
                    hash = (hash ^ (ok ? 1 : 0)) * 1099511628211ULL;
                }
                took = now() - start;
                if (r == 0 || took < best)
                    best = took;
            }
            printf("%-8s list %-12s patterns %3zu accepted %7zu hash %016llx "
                   "%.1f ns/group\n",
                   variants[v].name, fld[0], pats.n, accepted,
                   (unsigned long long) hash,
                   best * 1e9 / (groups.n ? groups.n : 1));
        }
        free(pats.v);
        free(line);
    }
    fclose(f);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "check") == 0)
        return run_check(argc - 2, argv + 2);
    if (argc >= 2 && strcmp(argv[1], "active") == 0)
        return run_active(argc - 2, argv + 2);
    fprintf(stderr, "usage: wildbench check [-l ms] [-v] FILE...\n"
                    "       wildbench active [-r reps] ACTIVE LISTS\n");
    return 2;
}
