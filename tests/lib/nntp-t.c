/*
 * nntp test suite: nntp_read_line's wait for data.
 *
 * Reads a line, times out with nothing to read, and sees end of file, on an
 * ordinary descriptor and on one above FD_SETSIZE, which a select()-based
 * wait cannot handle (macOS's select() fails on it; glibc's fortified
 * FD_SET aborts).
 */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"
#include "portable/socket.h"

#include <errno.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#include "inn/nntp.h"
#include "tap/basic.h"

/* Seconds elapsed since START, as a double. */
static double
since(const struct timeval *start)
{
    struct timeval now;

    gettimeofday(&now, NULL);
    return (now.tv_sec - start->tv_sec)
           + (now.tv_usec - start->tv_usec) / 1e6;
}

/* Runs the checks on the connected pair FDS, prefixing each label with
   WHAT.  The nntp struct owns fds[0]; fds[1] is closed here. */
static void
check_reads(int fds[2], const char *what)
{
    struct nntp *nntp;
    char *line;
    enum nntp_status status;
    struct timeval start;
    double elapsed;
    static const char data[] = "200 hello\r\n";

    nntp = nntp_new(fds[0], fds[0], 0, 1);
    if (write(fds[1], data, sizeof(data) - 1) != sizeof(data) - 1)
        sysbail("cannot write test data");
    status = nntp_read_line(nntp, &line);
    is_int(NNTP_READ_OK, status, "%s: read a line", what);
    if (status == NNTP_READ_OK)
        is_string("200 hello", line, "%s: the line", what);
    else
        ok(0, "%s: the line", what);

    gettimeofday(&start, NULL);
    status = nntp_read_line(nntp, &line);
    elapsed = since(&start);
    is_int(NNTP_READ_TIMEOUT, status, "%s: times out with nothing to read",
           what);
    ok(elapsed >= 0.9 && elapsed < 5, "%s: after the 1 s timeout (%.2f s)",
       what, elapsed);

    close(fds[1]);
    status = nntp_read_line(nntp, &line);
    is_int(NNTP_READ_EOF, status, "%s: end of file", what);
    nntp_free(nntp);
}

int
main(void)
{
    int fds[2];
    struct rlimit rl;
    int high;

    plan(10);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        sysbail("cannot create socketpair");
    check_reads(fds, "low descriptor");

    /* The same on a descriptor above FD_SETSIZE, if the limits allow. */
    high = FD_SETSIZE + 10;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
        sysbail("cannot get RLIMIT_NOFILE");
    if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur <= (rlim_t) high + 1) {
        if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max <= (rlim_t) high + 1) {
            skip_block(5, "descriptor limit %lu too low",
                       (unsigned long) rl.rlim_max);
            return 0;
        }
        rl.rlim_cur = high + 2;
        if (setrlimit(RLIMIT_NOFILE, &rl) < 0)
            sysbail("cannot raise RLIMIT_NOFILE");
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        sysbail("cannot create socketpair");
    if (dup2(fds[0], high) < 0)
        sysbail("cannot move the socket to %d", high);
    close(fds[0]);
    fds[0] = high;
    check_reads(fds, "descriptor above FD_SETSIZE");
    return 0;
}
