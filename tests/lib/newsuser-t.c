/*
 * Tests for the privilege drop in lib/newsuser.c.  The interesting paths run
 * only when started as root, so as any other user the test skips; it is run
 * as root by support/root-checks.sh (under sudo).  Each case runs in a child
 * process, since dropping root cannot be undone.
 */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "inn/messages.h"
#include "inn/newsuser.h"
#include "tap/basic.h"

/* Exit statuses from the children: 0 is success, others say what failed. */
enum {
    CHILD_OK = 0,
    CHILD_UID = 10,
    CHILD_GID = 11,
    CHILD_READ = 12,
    CHILD_NOT_REFUSED = 13,
    CHILD_NO_READ = 14
};

/* A file readable only through a group root has and the news user lacks. */
static char probe[256];

/* Run fn in a child and return its exit status (or -1 if it did not exit). */
static int
in_child(int (*fn)(void))
{
    pid_t pid;
    int status;

    fflush(stdout);
    pid = fork();
    if (pid < 0)
        sysbail("cannot fork");
    if (pid == 0) {
        /* die() in the library exits with status 1. */
        message_handlers_die(0);
        _exit(fn());
    }
    if (waitpid(pid, &status, 0) != pid)
        sysbail("cannot wait for child");
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/*
 * Find a supplementary group of this (root) process that is not the news
 * group and that the news user is not a member of.  Returns false if none.
 * getgroups() is fine here: before any setgroups() call, macOS's variant and
 * the kernel agree.
 */
static bool
find_root_only_group(gid_t *found)
{
    struct passwd *pw;
    uid_t uid;
    gid_t gid, mine[64];
    int nmine, nnews, i, j;
    gid_t news[64];

    if (get_news_uid_gid(&uid, &gid, false) < 0)
        return false;
    if ((pw = getpwuid(uid)) == NULL)
        return false;
    nmine = getgroups(64, mine);
    nnews = 64;
    if (getgrouplist(pw->pw_name, (int) gid, (int *) news, &nnews) < 0)
        return false;
    for (i = 0; i < nmine; i++) {
        bool shared = (mine[i] == gid);

        for (j = 0; j < nnews && !shared; j++)
            shared = (news[j] == mine[i]);
        if (!shared) {
            *found = mine[i];
            return true;
        }
    }
    return false;
}

/* Can the probe file be read?  (The kernel decides, not getgroups().) */
static bool
can_read_probe(void)
{
    int fd = open(probe, O_RDONLY);

    if (fd < 0)
        return false;
    close(fd);
    return true;
}

/* Drop with ensure_news_user_grp: right IDs, and root's groups are gone. */
static int
drop_all(void)
{
    uid_t uid;
    gid_t gid;

    if (get_news_uid_gid(&uid, &gid, false) < 0)
        return CHILD_UID;
    ensure_news_user_grp(true, true);
    if (getuid() != uid || geteuid() != uid)
        return CHILD_UID;
    if (getgid() != gid || getegid() != gid)
        return CHILD_GID;
    return can_read_probe() ? CHILD_READ : CHILD_OK;
}

/* The drop as it was before setgroups() was added: setgid, then setuid. */
static int
drop_without_setgroups(void)
{
    uid_t uid;
    gid_t gid;

    if (get_news_uid_gid(&uid, &gid, false) < 0)
        return CHILD_UID;
    if (setgid(gid) < 0 || setuid(uid) < 0)
        return CHILD_UID;
    return can_read_probe() ? CHILD_OK : CHILD_NO_READ;
}

/* ensure_news_user(false) must refuse to run as root. */
static int
refuse_user(void)
{
    ensure_news_user(false);
    return CHILD_NOT_REFUSED;
}

int
main(void)
{
    uid_t uid;
    gid_t gid, probe_gid;

    if (geteuid() != 0)
        skip_all("must be run as root (support/root-checks.sh)");
    if (get_news_uid_gid(&uid, &gid, false) < 0)
        skip_all("runasuser or runasgroup does not exist");
    if (uid == 0)
        skip_all("runasuser is root");

    plan(3);

    if (find_root_only_group(&probe_gid)) {
        char dir[] = "/tmp/newsuser-t.XXXXXX";
        int fd;

        if (mkdtemp(dir) == NULL || chmod(dir, 0711) < 0)
            sysbail("cannot create a probe directory");
        snprintf(probe, sizeof(probe), "%s/probe", dir);
        fd = open(probe, O_WRONLY | O_CREAT | O_EXCL, 0040);
        if (fd < 0 || fchown(fd, 0, probe_gid) < 0 || fchmod(fd, 0040) < 0)
            sysbail("cannot create the probe file");
        close(fd);
        diag("probe file readable only by group %lu",
             (unsigned long) probe_gid);
        is_int(CHILD_OK, in_child(drop_without_setgroups),
               "control: without setgroups, root's group survives the drop");
        is_int(CHILD_OK, in_child(drop_all),
               "dropping root sets the news IDs and drops root's groups");
        unlink(probe);
        rmdir(dir);
    } else {
        skip_block(2, "no group that root has and the news user lacks");
    }
    is_int(1, in_child(refuse_user),
           "ensure_news_user(false) refuses to run as root");
    return 0;
}
