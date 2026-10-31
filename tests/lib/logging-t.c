/*
 * Tests for lib/logging.cc: log messages to a file or to syslog, %m, escaped
 * control characters, reopening after rotation, symbolic links refused, and
 * whole lines from forked processes.  What goes to syslog itself is not
 * checked; only that it does not reach the file.
 */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>

#include "inn/logging.h"
#include "tap/basic.h"

/* The contents of a file, or an empty string if it cannot be read; the
   caller frees it. */
static char *
slurp(const char *path)
{
    FILE *f;
    char *buf;
    size_t size = 0, n;

    buf = bmalloc(64 * 1024);
    f = fopen(path, "r");
    if (f != NULL) {
        while ((n = fread(buf + size, 1, 64 * 1024 - 1 - size, f)) > 0)
            size += n;
        fclose(f);
    }
    buf[size] = '\0';
    return buf;
}

/* Whether the file contains needle. */
static int
has(const char *path, const char *needle)
{
    char *text = slurp(path);
    int found = strstr(text, needle) != NULL;

    free(text);
    return found;
}

/* The number of lines in the file. */
static int
lines(const char *path)
{
    char *text = slurp(path), *p;
    int count = 0;

    for (p = text; *p != '\0'; p++)
        if (*p == '\n')
            count++;
    free(text);
    return count;
}

int
main(void)
{
    char dir[] = "/tmp/logging-t.XXXXXX";
    char file[256], old[256], link_name[256], sub[256], subfile[256];
    char wanted[256];
    char *text;
    pid_t pid;
    int status;

    plan(23);
    if (mkdtemp(dir) == NULL)
        sysbail("cannot create a temporary directory");
    snprintf(file, sizeof(file), "%s/inn.log", dir);
    snprintf(old, sizeof(old), "%s/inn.log.old", dir);
    snprintf(link_name, sizeof(link_name), "%s/link.log", dir);
    snprintf(sub, sizeof(sub), "%s/sub", dir);
    snprintf(subfile, sizeof(subfile), "%s/sub/inn.log", dir);

    inn_openlog("logging-t", 0, LOG_USER);
    ok(inn_log_file(file), "logging to a file");

    /* One line: timestamp, identifier and PID, level, message. */
    inn_syslog(LOG_ERR, "hello %d", 5);
    snprintf(wanted, sizeof(wanted), " logging-t[%ld]: err: hello 5\n",
             (long) getpid());
    ok(has(file, wanted), "the message, identifier, PID and level");
    text = slurp(file);
    ok(text[0] >= '0' && text[0] <= '9' && text[4] == '-',
       "the line starts with the date");
    free(text);
    is_int(1, lines(file), "one line");

    /* %m is the text of errno; %%m is a literal %m; errno is kept. */
    errno = ENOENT;
    inn_syslog(LOG_NOTICE, "gone: %m");
    snprintf(wanted, sizeof(wanted), "notice: gone: %s\n", strerror(ENOENT));
    ok(has(file, wanted), "%%m is the text of errno");
    inn_syslog(LOG_INFO, "100%%m");
    ok(has(file, "info: 100%m\n"), "%%%%m is a literal %%m");
    errno = EACCES;
    inn_syslog(LOG_INFO, "errno kept");
    is_int(EACCES, errno, "errno is unchanged");

    /* Control characters are escaped, trailing newlines dropped. */
    inn_syslog(LOG_WARNING, "a\nb\rc\td");
    ok(has(file, "warning: a#012b#015c\td\n"),
       "newline and CR escaped, tab kept");
    inn_syslog(LOG_NOTICE, "trailing\n\n");
    ok(has(file, "notice: trailing\n") && !has(file, "trailing\n\n"),
       "trailing newlines dropped");
    is_int(6, lines(file), "one line per message");

    /* Rotation: after the file is moved, messages still go to it until it
       is reopened, then to a new file by the old name. */
    if (rename(file, old) != 0)
        sysbail("cannot rename %s", file);
    inn_syslog(LOG_NOTICE, "after rename");
    ok(has(old, "after rename"), "before reopening, to the moved file");
    ok(inn_log_reopen(), "reopened");
    inn_syslog(LOG_NOTICE, "after reopen");
    ok(has(file, "after reopen") && !has(old, "after reopen"),
       "after reopening, to a new file by the old name");

    /* A symbolic link is refused, and the destination does not change. */
    if (symlink(file, link_name) != 0)
        sysbail("cannot create %s", link_name);
    ok(!inn_log_file(link_name), "a symbolic link is refused");
    inn_syslog(LOG_NOTICE, "after link");
    ok(has(file, "after link"), "still logging to the file");

    /* A forked child and its parent each add whole lines. */
    pid = fork();
    if (pid < 0)
        sysbail("cannot fork");
    if (pid == 0) {
        inn_syslog(LOG_NOTICE, "from the child");
        _exit(0);
    }
    waitpid(pid, &status, 0);
    inn_syslog(LOG_NOTICE, "from the parent");
    snprintf(wanted, sizeof(wanted), " logging-t[%ld]: notice: from the child\n",
             (long) pid);
    ok(has(file, wanted), "the child's line, with its PID");
    ok(has(file, "notice: from the parent\n"), "the parent's line");
    is_int(4, lines(file), "the new file has whole lines only");

    /* Back to syslog: nothing more reaches the file. */
    ok(inn_log_file(NULL), "back to syslog");
    inn_syslog(LOG_DEBUG, "to syslog");
    ok(!has(file, "to syslog"), "not written to the file");

    /* A file that cannot be reopened: messages go to syslog until it can.
       Root can write anywhere, so this needs an ordinary user. */
    if (geteuid() == 0)
        skip_block(3, "running as root");
    else {
        if (mkdir(sub, 0700) != 0 || !inn_log_file(subfile))
            sysbail("cannot set up %s", subfile);
        unlink(subfile);
        chmod(sub, 0500);
        ok(!inn_log_reopen(), "reopening fails in an unwritable directory");
        inn_syslog(LOG_NOTICE, "while unwritable");
        chmod(sub, 0700);
        ok(inn_log_reopen(), "reopened once writable again");
        inn_syslog(LOG_NOTICE, "writable again");
        ok(has(subfile, "writable again") && !has(subfile, "while unwritable"),
           "messages resume in the file");
        inn_log_file(NULL);
    }

    inn_closelog();
    unlink(subfile);
    rmdir(sub);
    unlink(file);
    unlink(old);
    unlink(link_name);
    rmdir(dir);
    return 0;
}
