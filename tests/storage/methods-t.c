/*
 * Store, retrieve and cancel articles through the cnfs, timecaf, timehash
 * and trash storage methods via the storage manager API.
 *
 * Each method gets its own newsgroup and storage class in a generated
 * storage.conf, so SMstore routes the article to the method under test.
 * Retrieval is checked with articlemmap both on and off, since the methods
 * have separate mmap and read() code paths.
 */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>

#include "inn/buffer.h"
#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/storage.h"
#include "tap/basic.h"

/* Must match storage/methods.h, which is generated and private. */
#define TOKEN_TRASH    0
#define TOKEN_TIMEHASH 2
#define TOKEN_CNFS     3
#define TOKEN_TIMECAF  4

struct method {
    const char *name;
    const char *group;
    int type;
    int storageclass;
};

static const struct method methods[] = {
    {"cnfs",     "test.cnfs",     TOKEN_CNFS,     1},
    {"timecaf",  "test.timecaf",  TOKEN_TIMECAF,  2},
    {"timehash", "test.timehash", TOKEN_TIMEHASH, 3},
};

/* CNFS cycbuff size in KB. */
#define CYCBUFF_KB 4096

static void
write_file(const char *path, const char *contents)
{
    FILE *f = fopen(path, "w");

    if (f == NULL)
        sysbail("cannot create %s", path);
    if (fputs(contents, f) == EOF || fclose(f) == EOF)
        sysbail("cannot write %s", path);
}

static void
setup(const char *tmp)
{
    char *path, *conf;
    int fd;

    path = concat(tmp, "/etc", (char *) 0);
    if (mkdir(path, 0755) < 0)
        sysbail("cannot mkdir %s", path);
    free(path);
    path = concat(tmp, "/spool", (char *) 0);
    if (mkdir(path, 0755) < 0)
        sysbail("cannot mkdir %s", path);
    free(path);
    path = concat(tmp, "/db", (char *) 0);
    if (mkdir(path, 0755) < 0)
        sysbail("cannot mkdir %s", path);
    free(path);

    path = concat(tmp, "/etc/storage.conf", (char *) 0);
    write_file(path,
               "method cnfs {\n    newsgroups: test.cnfs\n    class: 1\n"
               "    options: TEST\n}\n"
               "method timecaf {\n    newsgroups: test.timecaf\n"
               "    class: 2\n}\n"
               "method timehash {\n    newsgroups: test.timehash\n"
               "    class: 3\n}\n"
               "method trash {\n    newsgroups: test.trash\n    class: 4\n}\n");
    free(path);

    /* A zero-filled cycbuff has no magic, so CNFS initializes it. */
    path = concat(tmp, "/spool/cycbuff", (char *) 0);
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || ftruncate(fd, (off_t) CYCBUFF_KB * 1024) < 0 || close(fd))
        sysbail("cannot create %s", path);
    xasprintf(&conf, "cycbuff:ONE:%s:%d\nmetacycbuff:TEST:ONE\n", path,
              CYCBUFF_KB);
    free(path);
    path = concat(tmp, "/etc/cycbuff.conf", (char *) 0);
    write_file(path, conf);
    free(path);
    free(conf);

    innconf = xcalloc(1, sizeof(*innconf));
    innconf->articlemmap = true;
    innconf->cnfscheckfudgesize = 0;
    innconf->maxartsize = 1000000;
    innconf->nfswriter = false;
    innconf->patharticles = concat(tmp, "/spool", (char *) 0);
    innconf->pathdb = concat(tmp, "/db", (char *) 0);
    innconf->pathetc = concat(tmp, "/etc", (char *) 0);
    innconf->pathspool = concat(tmp, "/spool", (char *) 0);
}

/*
 * Build a wire-format article posted to group, with a body of about
 * bodysize bytes, and fill in an ARTHANDLE pointing at it.
 */
static char *
make_article(const char *group, const char *id, size_t bodysize,
             ARTHANDLE *art)
{
    struct buffer *b = buffer_new();
    const char *groups;
    char *data;
    size_t i, len;

    buffer_append_sprintf(b,
                          "Path: test!not-for-mail\r\n"
                          "From: tester@example.com\r\n"
                          "Newsgroups: %s\r\n"
                          "Subject: storage method test\r\n"
                          "Message-ID: <%s@example.com>\r\n"
                          "Date: Wed, 28 Oct 2026 12:00:00 +0000\r\n"
                          "\r\n",
                          group, id);
    for (i = 0; i < bodysize / 64 + 1; i++)
        buffer_append_sprintf(b, "%s line %05lu of the test article body.\r\n",
                              id, (unsigned long) i);
    buffer_append(b, ".\r\n", 3);
    len = b->left;
    data = xmalloc(len);
    memcpy(data, b->data + b->used, len);
    buffer_free(b);

    groups = strstr(data, "Newsgroups: ") + strlen("Newsgroups: ");
    memset(art, 0, sizeof(*art));
    art->type = TOKEN_EMPTY;
    art->data = data;
    art->len = len;
    /* Methods write the iovec, as innd fills it in; data is for retrieval. */
    art->iov = xmalloc(sizeof(struct iovec));
    art->iov->iov_base = data;
    art->iov->iov_len = len;
    art->iovcnt = 1;
    art->arrived = time(NULL);
    art->expires = 0;
    art->groups = (char *) groups;
    art->groupslen = strcspn(groups, "\r\n");
    art->token = NULL;
    return data;
}

/* Retrieve token in full and check it matches data/len.  Two tests. */
static void
check_retrieve(const char *name, const char *what, TOKEN token,
               const char *data, size_t len)
{
    ARTHANDLE *got = SMretrieve(token, RETR_ALL);

    ok(got != NULL, "%s: %s retrieve succeeds", name, what);
    ok(got != NULL && got->len == len && memcmp(got->data, data, len) == 0,
       "%s: %s retrieve returns the stored article", name, what);
    if (got != NULL)
        SMfreearticle(got);
}

/* Nine tests for each method. */
static void
test_method(const struct method *m)
{
    ARTHANDLE art, *got;
    TOKEN token, big;
    char *data, *bigdata;
    struct iovec *iov, *bigiov;
    size_t len, biglen;
    bool mmap_setting = innconf->articlemmap;

    data = make_article(m->group, m->name, 300, &art);
    len = art.len;
    iov = art.iov;
    token = SMstore(art);
    is_int(m->type, token.type, "%s: stored with its token type", m->name);
    if (token.type != m->type)
        diag("SMstore error: %s", SMerrorstr ? SMerrorstr : "(none)");
    is_int(m->storageclass, token.class, "%s: stored with its class",
           m->name);

    innconf->articlemmap = true;
    check_retrieve(m->name, "mmap", token, data, len);
    innconf->articlemmap = false;
    check_retrieve(m->name, "read()", token, data, len);
    innconf->articlemmap = mmap_setting;

    got = SMretrieve(token, RETR_STAT);
    ok(got != NULL, "%s: RETR_STAT finds the article", m->name);
    if (got != NULL)
        SMfreearticle(got);

    /* A larger article, spanning many blocks/pages. */
    bigdata = make_article(m->group, "big", 64 * 1024, &art);
    biglen = art.len;
    bigiov = art.iov;
    big = SMstore(art);
    check_retrieve(m->name, "64KB", big, bigdata, biglen);

    ok(SMcancel(token), "%s: cancel succeeds", m->name);
    /* timecaf_cancel() only queues the cancel; DoCancels() applies it on
       SMflushcacheddata(), as INN's own callers do.  Same in the original C,
       so this flush is required, not a workaround. */
    SMflushcacheddata(SM_CANCELLEDART);
    got = SMretrieve(token, RETR_ALL);
    ok(got == NULL, "%s: cancelled article is gone", m->name);
    if (got != NULL)
        SMfreearticle(got);

    free(data);
    free(bigdata);
    free(iov);
    free(bigiov);
}

int
main(void)
{
    ARTHANDLE art;
    TOKEN token;
    char *tmp, *cmd, *data;
    bool value = true;
    size_t i;

    plan(3 * 11 + 4);

    tmp = test_tmpdir();
    xasprintf(&cmd, "rm -rf %s/etc %s/spool %s/db", tmp, tmp, tmp);
    if (system(cmd) < 0)
        sysbail("cannot clean %s", tmp);
    setup(tmp);

    if (!SMsetup(SM_RDWR, &value) || !SMsetup(SM_PREOPEN, &value))
        bail("cannot set up storage manager");
    if (!SMinit())
        bail("cannot initialize storage manager: %s", SMerrorstr);

    for (i = 0; i < ARRAY_SIZE(methods); i++)
        test_method(&methods[i]);

    /* trash discards everything. */
    data = make_article("test.trash", "trash", 300, &art);
    token = SMstore(art);
    is_int(TOKEN_TRASH, token.type, "trash: stored with its token type");
    is_int(4, token.class, "trash: stored with its class");
    ok(SMretrieve(token, RETR_ALL) == NULL && SMerrno == SMERR_NOENT,
       "trash: retrieve reports no such article");
    ok(!SMcancel(token), "trash: cancel fails");
    free(data);
    free(art.iov);

    SMshutdown();
    if (system(cmd) < 0)
        sysdiag("cannot clean %s", tmp);
    free(cmd);
    test_tmpdir_free(tmp);
    return 0;
}
