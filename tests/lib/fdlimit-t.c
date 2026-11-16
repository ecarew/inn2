/*  Test suite for the file descriptor limit functions.
**
**  setfdlimit no longer refuses limits above FD_SETSIZE, and isvalidfd
**  checks against the current limit rather than FD_SETSIZE.
*/

#include "portable/system.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>

#include "inn/libinn.h"
#include "tap/basic.h"

int
main(void)
{
    struct rlimit rl;
    rlim_t want = FD_SETSIZE + 64;
    int fd, high, limit;

    test_init(9);

    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
        sysbail("cannot get RLIMIT_NOFILE");
    limit = getfdlimit();
    ok(1, limit > 0);
    ok_int(2, (int) rl.rlim_cur, limit);

    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < want) {
        skip_block(3, 7, "hard descriptor limit below FD_SETSIZE + 64");
        return 0;
    }

    /* Above FD_SETSIZE: allowed now. */
    ok_int(3, 0, setfdlimit((unsigned int) want));
    ok_int(4, (int) want, getfdlimit());

    /* A real descriptor above FD_SETSIZE is valid; the limit itself is not. */
    fd = open("/dev/null", O_RDONLY);
    if (fd < 0)
        sysbail("cannot open /dev/null");
    high = FD_SETSIZE + 10;
    if (dup2(fd, high) < 0)
        sysbail("cannot dup2 to %d", high);
    ok(5, isvalidfd(high));
    ok(6, isvalidfd(0));
    ok(7, !isvalidfd((unsigned int) want));
    close(high);
    close(fd);

    /* Lowering works too, which innd relies on to fit its channel table. */
    ok_int(8, 0, setfdlimit(FD_SETSIZE + 32));
    ok(9, !isvalidfd(FD_SETSIZE + 40));
    return 0;
}
