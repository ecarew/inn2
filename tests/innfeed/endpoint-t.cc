/*
 * Test suite for innfeed's EndPoint event loop (innfeed/endpoint.cc): its
 * timers (order, removeTimeout, updateSleep, prepareWake), reads and writes
 * through socket pairs (a read of a minimum length arriving in pieces, end
 * of file, a write of several buffers, a large write reporting progress, a
 * write to a closed peer), a regular file read as an endpoint, work
 * callbacks, an endpoint deleted inside its own callback, signal handlers
 * run from the loop, and Run() returning when nothing is left.
 *
 * Built with innfeed/endpoint.cc and innfeed/buffer.cc; the few innfeed
 * symbols they use from elsewhere are defined here.
 */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <vector>

#include "inn/fdflag.h"
#include "inn/innconf.h"
#include "inn/libinn.h"
#include "tap/basic.h"

#include "innfeed.h"
#include "buffer.h"
#include "configfile.h"
#include "endpoint.h"
#include "host.h"

/* What endpoint.cc and buffer.cc need from the rest of innfeed. */
std::optional<std::string> InputFile;
scope *topScope = nullptr;
unsigned int loggingLevel = 0;

void
gCalcHostBlStat(void)
{
}

int
getInteger(scope *, const char *, long *, int)
{
    return 0;
}

void
d_printf(unsigned int, const char *, ...)
{
}

void
logOrPrint(int, FILE *, const char *, ...)
{
}

const char *
boolToString(bool val)
{
    return val ? "true" : "false";
}

namespace {

std::vector<std::string> events;

void
stop(TimeoutId)
{
    stopRun();
}

void
record(TimeoutId, std::string *name)
{
    events.push_back(*name);
}

/* Run the loop, with a stop timer after secs seconds as a backstop. */
void
runFor(int secs)
{
    TimeoutId backstop = prepareSleep<stop>(secs);
    Run();
    removeTimeout(backstop);
}

std::string
contents(Buffer *buffs)
{
    std::string s;

    for (int i = 0; buffs != nullptr && buffs[i] != nullptr; i++)
        s.append(static_cast<char *>(bufferBase(buffs[i])),
                 bufferDataSize(buffs[i]));
    return s;
}

Buffer
textBuffer(const std::string &text)
{
    Buffer b = newBuffer(text.size() + 1);

    memcpy(bufferBase(b), text.data(), text.size());
    bufferSetDataSize(b, text.size());
    return b;
}

/* A completed read or write. */
struct Done {
    int calls = 0;
    IoStatus status = IoIncomplete;
    std::string data;
    int err = 0;
    bool stopAfter = true;
};

void
readDone(EndPoint e, IoStatus s, Buffer *b, Done *d)
{
    d->calls++;
    d->status = s;
    d->data = contents(b);
    d->err = endPointErrno(e);
    freeBufferArray(b);
    if (d->stopAfter)
        stopRun();
}

void
writeDone(EndPoint e, IoStatus s, Buffer *b, Done *d)
{
    d->calls++;
    d->status = s;
    d->err = endPointErrno(e);
    freeBufferArray(b);
    if (d->stopAfter)
        stopRun();
}

/* prepareWake and a write with a progress callback have no typed forms in
   endpoint.h; these use the untyped API with its trampolines. */
TimeoutId
wake(time_t when, std::string *name)
{
    return prepareWake(&endpoint_detail::timer<record, std::string>, when,
                       name);
}

void writeProgress(EndPoint, IoStatus s, Buffer *, Done *);

int
writeWithProgress(EndPoint e, Buffer *buffs, Done *d)
{
    return prepareWrite(e, buffs, endpRW<writeProgress, Done>,
                        endpRW<writeDone, Done>, d);
}

int progressCalls = 0;

void
writeProgress(EndPoint, IoStatus s, Buffer *, Done *)
{
    if (s == IoProgress)
        progressCalls++;
}

/* For the timer that writes the rest of a split message. */
struct Later {
    int fd;
    std::string text;
};

void
writeLater(TimeoutId, Later *l)
{
    if (write(l->fd, l->text.data(), l->text.size()) < 0)
        sysdiag("write");
}

int workRuns = 0;

void
work(EndPoint e, int *limit)
{
    workRuns++;
    if (workRuns < *limit)
        addWorkCallback<work>(e, limit);
    else
        stopRun();
}

int sigRuns = 0;

void
onSignal(int)
{
    sigRuns++;
}

void
raiseAndPoke(TimeoutId, int *fd)
{
    kill(getpid(), SIGUSR1);
    if (write(*fd, "x", 1) != 1)
        sysdiag("write");
}

void
deleteSelf(EndPoint e, IoStatus, Buffer *b, Done *d)
{
    d->calls++;
    freeBufferArray(b);
    delEndPoint(e);
}

void
pair(int sv[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        sysbail("socketpair");
    /* Non-blocking, as innfeed makes its connections (connection.cc). */
    fdflag_nonblocking(sv[0], true);
    fdflag_nonblocking(sv[1], true);
}

} // namespace

int
main()
{
    innconf = static_cast<struct innconf *>(xcalloc(1, sizeof *innconf));
    stdioFdMax = 0;
    /* innfeed sets its handlers before Run(); the loop expects that. */
    setSigHandler(SIGUSR1, onSignal);
    plan(35);

    /* Timers fire in time order, equal times in the order they were set;
       prepareWake takes an absolute time. */
    std::string a = "a", b = "b", c = "c", d = "d";
    time_t now = theTime();
    prepareSleep<record>(2, &a);
    prepareSleep<record>(1, &b);
    wake(now + 1, &c);
    prepareSleep<record>(1, &d);
    prepareSleep<stop>(3);
    Run();
    is_int(4, events.size(), "four timers fired");
    is_string("b c d a",
              events.size() == 4 ? (events[0] + " " + events[1] + " "
                                    + events[2] + " " + events[3])
                                       .c_str()
                                 : "",
              "timers in time order, ties in the order set");
    is_int(0, wake(theTime() - 1, &a),
           "prepareWake in the past returns 0");

    /* removeTimeout and updateSleep. */
    events.clear();
    TimeoutId gone = prepareSleep<record>(1, &a);
    TimeoutId moved = prepareSleep<record>(1, &b);
    ok(removeTimeout(gone), "removeTimeout of a pending timer");
    ok(!removeTimeout(gone), "removeTimeout of a removed timer fails");
    ok(removeTimeout(0), "removeTimeout(0) is allowed");
    TimeoutId moved2 = updateSleep<record>(moved, 2, &c);
    ok(moved2 != 0 && moved2 != moved, "updateSleep returns a new id");
    ok(!removeTimeout(moved), "updateSleep removed the old timer");
    prepareSleep<stop>(3);
    Run();
    is_string("c", events.size() == 1 ? events[0].c_str() : "",
              "only the updated timer fired");

    /* A read of at least 7 bytes, arriving as 3 then 4 a second apart. */
    int sv[2];
    pair(sv);
    EndPoint e = newEndPoint(sv[0]);
    ok(e != nullptr, "newEndPoint on a socket");
    Done r;
    Buffer *rb = makeBufferArray(newBuffer(64), NULL);
    ok(prepareRead<readDone>(e, rb, &r, 7), "prepareRead");
    ok(readIsPending(e), "readIsPending");
    Buffer *rb2 = makeBufferArray(newBuffer(8), NULL);
    is_int(0, prepareRead<readDone>(e, rb2, &r, 1),
           "a second prepareRead is refused");
    freeBufferArray(rb2);
    if (write(sv[1], "abc", 3) != 3)
        sysbail("write");
    Later later = {sv[1], "defg"};
    prepareSleep<writeLater>(1, &later);
    runFor(5);
    is_int(1, r.calls, "read callback once");
    ok(r.status == IoDone, "read done");
    is_string("abcdefg", r.data.c_str(), "read data across two arrivals");
    ok(!readIsPending(e), "no read pending after completion");

    /* End of file. */
    Done eof;
    prepareRead<readDone>(e, makeBufferArray(newBuffer(16), NULL), &eof, 1);
    close(sv[1]);
    runFor(5);
    ok(eof.calls == 1 && eof.status == IoEOF, "read reports end of file");
    delEndPoint(e);

    /* A write of three buffers, read back by a second endpoint. */
    pair(sv);
    EndPoint w = newEndPoint(sv[0]);
    EndPoint rd = newEndPoint(sv[1]);
    Done wd, wr;
    wd.stopAfter = false;
    ok(writeWithProgress(
           w, makeBufferArray(textBuffer("one "), textBuffer("two "),
                              textBuffer("three"), NULL),
           &wd),
       "prepareWrite of three buffers");
    ok(writeIsPending(w), "writeIsPending");
    prepareRead<readDone>(rd, makeBufferArray(newBuffer(64), NULL), &wr, 13);
    runFor(5);
    ok(wd.calls == 1 && wd.status == IoDone, "write done");
    is_string("one two three", wr.data.c_str(), "peer read all three buffers");

    /* A write larger than the socket buffer reports progress; the peer
       drains it as it arrives. */
    const size_t big = 4 * 1024 * 1024;
    std::string bigText(big, 'x');
    Done bw, br;
    bw.stopAfter = false;
    progressCalls = 0;
    writeWithProgress(
        w, makeBufferArray(textBuffer(bigText), NULL), &bw);
    prepareRead<readDone>(rd, makeBufferArray(newBuffer(big), NULL), &br,
                          big);
    runFor(10);
    ok(bw.calls == 1 && bw.status == IoDone, "large write done");
    ok(progressCalls > 0, "large write reported progress (%d)",
       progressCalls);
    is_int(big, br.data.size(), "peer read all of the large write");

    /* A write to a closed peer fails with EPIPE (Run ignores SIGPIPE). */
    delEndPoint(rd);
    Done fw;
    writeWithProgress(
        w, makeBufferArray(textBuffer(std::string(1024 * 1024, 'y')), NULL),
        &fw);
    runFor(5);
    ok(fw.calls == 1 && fw.status == IoFailed, "write to a closed peer fails");
    is_int(EPIPE, fw.err, "with EPIPE");
    delEndPoint(w);

    /* A regular file read as an endpoint: its data, then end of file. */
    char path[] = "/tmp/endpoint-t-XXXXXX";
    int ffd = mkstemp(path);
    if (ffd < 0 || write(ffd, "file data\n", 10) != 10)
        sysbail("cannot make %s", path);
    lseek(ffd, 0, SEEK_SET);
    unlink(path);
    EndPoint fe = newEndPoint(ffd);
    Done fr, fe2;
    prepareRead<readDone>(fe, makeBufferArray(newBuffer(64), NULL), &fr, 1);
    runFor(5);
    is_string("file data\n", fr.data.c_str(), "regular file read");
    prepareRead<readDone>(fe, makeBufferArray(newBuffer(64), NULL), &fe2, 1);
    runFor(5);
    ok(fe2.calls == 1 && fe2.status == IoEOF, "regular file end of file");
    delEndPoint(fe);

    /* Work callbacks: run while registered, without waiting for the
       loop's timers (the backstop is 5 seconds away). */
    pair(sv);
    EndPoint we = newEndPoint(sv[0]);
    int limit = 100;
    time_t before = time(nullptr);
    addWorkCallback<work>(we, &limit);
    runFor(5);
    is_int(100, workRuns, "work callback re-registered 100 times");
    ok(time(nullptr) - before < 3, "work callbacks did not wait on timers");

    /* An endpoint deleted inside its own read callback; another endpoint
       is still served afterwards. */
    EndPoint other = newEndPoint(sv[1]);
    Done del, after;
    prepareRead<deleteSelf>(we, makeBufferArray(newBuffer(8), NULL), &del,
                            1);
    if (write(sv[1], "z", 1) != 1)
        sysbail("write");
    runFor(2);
    is_int(1, del.calls, "endpoint deleted in its callback");
    /* sv[0] is closed now: the other end sees end of file. */
    prepareRead<readDone>(other, makeBufferArray(newBuffer(8), NULL), &after,
                          1);
    runFor(5);
    ok(after.calls == 1 && after.status == IoEOF,
       "the other endpoint is still served");
    delEndPoint(other);

    /* A signal handler set with setSigHandler runs from the loop. */
    pair(sv);
    EndPoint se = newEndPoint(sv[0]);
    Done sr;
    sr.stopAfter = false;
    prepareRead<readDone>(se, makeBufferArray(newBuffer(8), NULL), &sr, 1);
    prepareSleep<raiseAndPoke>(1, &sv[1]);
    runFor(3);
    is_int(1, sigRuns, "signal handler ran once from the loop");
    delEndPoint(se);
    close(sv[1]);

    /* With no endpoints and no timers, Run returns. */
    Run();
    ok(true, "Run returns with nothing to do");

    return 0;
}
