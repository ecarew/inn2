/* Test suite for general channel handling.

   CHANreadloop never returns, so the scenarios run one after another from
   inside it: each one's last callback starts the next, and the last one
   exits.  A "timer" is a channel on a pipe put to sleep with SCHANadd; its
   waker runs the next step. */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <fcntl.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#include <time.h>

#include <string>
#include <vector>

#include "inn/buffer.h"
#include "inn/fdflag.h"
#include "inn/innconf.h"
#include "inn/libinn.h"
#include "tap/basic.h"

#include "../../innd/innd.h"

namespace {

void scenario_blocked_write();
void scenario_wake_order();
void scenario_close_in_reader();
void scenario_priority();
void scenario_alternate();
void scenario_replace_fd();

/* Initialize things enough to be able to call channel functions.  This only
   has to be called once. */
void
initialize()
{
    if (access("../data/etc/inn.conf", F_OK) < 0)
        if (access("data/etc/inn.conf", F_OK) == 0)
            if (chdir("innd") != 0)
                sysdie("cannot cd to innd");
    if (!innconf_read("../data/etc/inn.conf"))
        exit(1);
    Log = fopen("/dev/null", "w");
    if (Log == nullptr)
        sysdie("cannot open /dev/null");
    CHANsetup(64);
    gettimeofday(&Now, nullptr);

    /* One pass a second at most while idle, short write backoff, and the
       default command read limit made explicit. */
    TimeOut.tv_sec = 1;
    TimeOut.tv_usec = 0;
    innconf->timer = 0;
    innconf->blockbackoff = 1;
    innconf->maxcmdreadsize = 10;
}

/* A non-blocking socket pair, as innd's network channels are. */
void
make_pair(int sv[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
        sysdie("cannot create socket pair");
    if (!fdflag_nonblocking(sv[0], true) || !fdflag_nonblocking(sv[1], true))
        sysdie("cannot make socket pair nonblocking");
}

void
put(int fd, const char *data)
{
    if (write(fd, data, strlen(data)) != static_cast<ssize_t>(strlen(data)))
        sysdie("cannot write test data");
}

void
noread(CHANNEL *cp UNUSED)
{
}

void
nowrite(CHANNEL *cp UNUSED)
{
}

/* Run next after the given number of seconds, from a sleeping channel. */
using step_func = void (*)();
std::vector<step_func> timer_steps(64, nullptr);

void
timer_wake(CHANNEL *cp)
{
    step_func next = timer_steps[cp->fd];
    int fd = cp->fd;

    timer_steps[fd] = nullptr;
    CHANclose(cp, CHANname(cp));
    next();
}

void
after(int seconds, step_func next)
{
    int p[2];

    if (pipe(p) < 0)
        sysdie("cannot create pipe");
    close(p[1]);
    CHANNEL *cp = CHANcreate(p[0], CTany, CSwriting, noread, nowrite);
    timer_steps[p[0]] = next;
    SCHANadd(cp, Now.tv_sec + seconds, nullptr, timer_wake, nullptr);
}


/* Scenario 1 (the original test): write a file, read it back, sleep a
   channel and wake it by event. */
void reader(CHANNEL *cp);
void writedone(CHANNEL *cp);
void wakeup(CHANNEL *cp);

void
writedone(CHANNEL *cp)
{
    ok(true, "write completed");
    is_int(0, cp->Out.left, "nothing left to write");
    int fd = open("output", O_RDONLY | O_EXCL);
    if (fd < 0)
        sysdie("cannot open output");
    CHANNEL *newcp = CHANcreate(fd, CTany, CSgetbody, reader, nullptr);
    RCHANadd(newcp);
    ok(!CHANsleeping(newcp), "reader is not sleeping");
    SCHANadd(cp, time(nullptr) + 60, &Now, wakeup,
             xstrdup("some random text"));
    ok(CHANsleeping(cp), "writer is sleeping");
}

void
reader(CHANNEL *cp)
{
    ok(true, "reader called");
    is_int(strlen("some output"), CHANreadtext(cp), "read the whole file");
    is_int(strlen("some output"), cp->In.used, "buffer holds it");
    /* FIXME: As long as the In buffer does not use the normal meanings of
       .used and .left, we cannot use buffer_append. */
    memcpy(cp->In.data + cp->In.used, "", 1);
    cp->In.used++;
    cp->In.left--;
    is_string("some output", cp->In.data, "file contents");
    CHANclose(cp, CHANname(cp));
    SCHANwakeup(&Now);}

void
wakeup(CHANNEL *cp)
{
    ok(true, "sleeper woken by event");
    is_string("some random text", static_cast<const char *>(cp->Argument),
              "waker argument");
    CHANclose(cp, CHANname(cp));
    is_int(CTfree, cp->Type, "closed channel is free");
    unlink("output");
    scenario_blocked_write();
}


/* Scenario 2: a write larger than the socket buffer waits while the peer
   is not reading (the descriptor does not select writable, so this is not
   the EAGAIN sleep path), and everything arrives once the peer drains it. */
struct {
    int sv[2];
    CHANNEL *writer;
    CHANNEL *reader;
    std::string data;
    size_t received;
    bool corrupt;
    bool written;
} bw;

void
bw_check_done()
{
    if (!bw.written || bw.received < bw.data.size())
        return;
    ok(!bw.corrupt, "blocked write: data arrived intact");
    is_int(bw.data.size(), bw.received, "blocked write: all bytes arrived");
    CHANclose(bw.writer, CHANname(bw.writer));
    CHANclose(bw.reader, CHANname(bw.reader));
    scenario_wake_order();
}

void
bw_read(CHANNEL *cp)
{
    int n = CHANreadtext(cp);

    if (n <= 0) {
        if (n == -2)
            return;
        ok(false, "blocked write: reader saw end or error");
        CHANclose(cp, CHANname(cp));
        return;
    }
    if (bw.data.compare(bw.received, n, cp->In.data, n) != 0)
        bw.corrupt = true;
    bw.received += n;
    cp->In.used = 0;
    bw_check_done();
}

void
bw_writedone(CHANNEL *cp UNUSED)
{
    bw.written = true;
    bw_check_done();
}

void
bw_drain()
{
    ok(!bw.written && bw.writer->Out.left > 0,
       "blocked write: writer waits while the peer is not reading");
    ok(bw.received == 0, "blocked write: nothing read yet");
    bw.reader = CHANcreate(bw.sv[1], CTany, CSgetbody, bw_read, nowrite);
    RCHANadd(bw.reader);
}

void
scenario_blocked_write()
{
    make_pair(bw.sv);
    bw.data.resize(4 * 1024 * 1024);
    for (size_t i = 0; i < bw.data.size(); i++)
        bw.data[i] = static_cast<char>(i % 251);
    bw.writer = CHANcreate(bw.sv[0], CTany, CSwriting, noread, bw_writedone);
    WCHANset(bw.writer, bw.data.data(), bw.data.size());
    WCHANadd(bw.writer);
    after(2, bw_drain);
}


/* Scenario 3: sleepers wake in the order of their wake times. */
std::vector<int> wake_order;
CHANNEL *sleepers[3];

void
wo_wake(CHANNEL *cp)
{
    for (int i = 0; i < 3; i++)
        if (sleepers[i] == cp)
            wake_order.push_back(i);
    CHANclose(cp, CHANname(cp));
    if (wake_order.size() < 3)
        return;
    ok(wake_order[2] == 0, "wake order: the latest sleeper wakes last");
    ok(wake_order[0] != 0 && wake_order[1] != 0,
       "wake order: the two earlier sleepers wake first");
    scenario_close_in_reader();
}

void
scenario_wake_order()
{
    static const int delay[3] = {3, 1, 1};

    for (int i = 0; i < 3; i++) {
        int p[2];

        if (pipe(p) < 0)
            sysdie("cannot create pipe");
        close(p[1]);
        sleepers[i] = CHANcreate(p[0], CTany, CSwriting, noread, nowrite);
        SCHANadd(sleepers[i], Now.tv_sec + delay[i], nullptr, wo_wake,
                 nullptr);
    }
    ok(CHANsleeping(sleepers[0]) && CHANsleeping(sleepers[2]),
       "wake order: all three sleeping");
}


/* Scenario 4: a channel closed inside its own reader is not called again. */
struct {
    int sv[2];
    CHANNEL *cp;
    int calls;
} cr;

void
cr_read(CHANNEL *cp)
{
    cr.calls++;
    CHANreadtext(cp);
    CHANclose(cp, CHANname(cp));
}

void
cr_check()
{
    is_int(1, cr.calls, "close in reader: reader called once");
    is_int(CTfree, cr.cp->Type, "close in reader: channel is free");
    close(cr.sv[1]);
    scenario_priority();
}

void
scenario_close_in_reader()
{
    make_pair(cr.sv);
    cr.cp = CHANcreate(cr.sv[0], CTany, CSgetbody, cr_read, nowrite);
    RCHANadd(cr.cp);
    put(cr.sv[1], "first");
    after(2, cr_check);
}


/* Scenario 5: a control channel is served before other ready channels. */
struct {
    int any[2];
    int control[2];
    CHANNEL *any_cp;
    CHANNEL *control_cp;
    std::string order;
} pr;

void
pr_done()
{
    if (pr.order.size() < 2)
        return;
    is_string("ca", pr.order.c_str(), "priority: control channel first");
    CHANclose(pr.any_cp, CHANname(pr.any_cp));
    CHANclose(pr.control_cp, CHANname(pr.control_cp));
    close(pr.any[1]);
    close(pr.control[1]);
    scenario_alternate();
}

void
pr_read_any(CHANNEL *cp)
{
    CHANreadtext(cp);
    cp->In.used = 0;
    pr.order += 'a';
    pr_done();
}

void
pr_read_control(CHANNEL *cp)
{
    CHANreadtext(cp);
    cp->In.used = 0;
    pr.order += 'c';
    pr_done();
}

void
pr_send()
{
    put(pr.any[1], "x");
    put(pr.control[1], "y");
}

void
scenario_priority()
{
    make_pair(pr.any);
    make_pair(pr.control);
    pr.any_cp = CHANcreate(pr.any[0], CTany, CSgetbody, pr_read_any, nowrite);
    pr.control_cp = CHANcreate(pr.control[0], CTcontrol, CSgetbody,
                               pr_read_control, nowrite);
    RCHANadd(pr.any_cp);
    RCHANadd(pr.control_cp);
    after(1, pr_send);
}


/* Scenario 6: two busy command readers take turns, each reading at most
   maxcmdreadsize bytes a turn. */
struct {
    int a[2];
    int b[2];
    CHANNEL *a_cp;
    CHANNEL *b_cp;
    std::string order;
    size_t a_bytes;
    size_t b_bytes;
    bool over_limit;
} al;

void
al_read(CHANNEL *cp)
{
    int n = CHANreadtext(cp);

    if (n <= 0)
        return;
    if (n > 10)
        al.over_limit = true;
    cp->In.used = 0;
    if (cp == al.a_cp) {
        al.order += 'a';
        al.a_bytes += n;
    } else {
        al.order += 'b';
        al.b_bytes += n;
    }
    if (al.a_bytes < 50 || al.b_bytes < 50)
        return;

    bool alternates = true;
    for (size_t i = 1; i < 10 && i < al.order.size(); i++)
        if (al.order[i] == al.order[i - 1])
            alternates = false;
    ok(alternates, "alternate: readers take turns (%s)", al.order.c_str());
    ok(!al.over_limit, "alternate: no read over maxcmdreadsize");
    is_int(10, al.order.size(), "alternate: five reads each");
    CHANclose(al.a_cp, CHANname(al.a_cp));
    CHANclose(al.b_cp, CHANname(al.b_cp));
    close(al.a[1]);
    close(al.b[1]);
    scenario_replace_fd();
}

void
al_send()
{
    static const char fifty[] =
        "0123456789012345678901234567890123456789012345678\n";

    put(al.a[1], fifty);
    put(al.b[1], fifty);
}

void
scenario_alternate()
{
    make_pair(al.a);
    make_pair(al.b);
    al.a_cp = CHANcreate(al.a[0], CTany, CSgetcmd, al_read, nowrite);
    al.b_cp = CHANcreate(al.b[0], CTany, CSgetcmd, al_read, nowrite);
    RCHANadd(al.a_cp);
    RCHANadd(al.b_cp);
    after(1, al_send);
}


/* Scenario 7: a channel's descriptor replaced in place, as SITEspool does
   when a site falls back to spooling, then flushed synchronously. */
std::string
slurp(const char *path)
{
    std::string s;
    char buf[256];
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0)
        return s;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        s.append(buf, n);
    close(fd);
    return s;
}

void
scenario_replace_fd()
{
    int fd = open("spool1", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        sysdie("cannot create spool1");
    CHANNEL *cp = CHANcreate(fd, CTany, CSwriting, noread, nowrite);

    int i = open("spool2", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (i < 0)
        sysdie("cannot create spool2");
    WCHANremove(cp);
    RCHANremove(cp);
    SCHANremove(cp);
    close(cp->fd);
    cp->fd = i;
    WCHANset(cp, "spooled\n", strlen("spooled\n"));
    ok(WCHANflush(cp), "replace fd: flush succeeds");
    is_string("spooled\n", slurp("spool2").c_str(),
              "replace fd: data went to the new descriptor");
    CHANclose(cp, CHANname(cp));
    unlink("spool1");
    unlink("spool2");

    CHANshutdown();
    exit(0);
}

} // namespace


int
main()
{
    plan(30);
    initialize();
    message_handlers_notice(0);
    message_handlers_warn(0);

    /* If a scenario never finishes, end the test rather than hang. */
    alarm(60);

    int fd = open("output", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        sysdie("cannot create output");
    CHANNEL *cp = CHANcreate(fd, CTany, CSwriting, reader, writedone);
    is_int(fd, cp->fd, "channel holds its descriptor");
    is_int(CTany, cp->Type, "channel type");
    is_int(CSwriting, cp->State, "channel state");
    ok(!CHANsleeping(cp), "new channel is not sleeping");
    WCHANset(cp, "some output", strlen("some output"));
    WCHANadd(cp);
    CHANreadloop();

    /* CHANreadloop should never return. */
    die("fell through main");
}
