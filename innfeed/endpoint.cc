/*
**  The implementation of the innfeed EndPoint object class.
**
**  Written by James Brister <brister@vix.com>
**
**  The EndPoint class is what gives the illusion (sort of, kind of) of
**  threading.  Basically it controls an event loop (Boost.Asio since
**  ASIO_PLAN.md phase 3; a select loop before) and a set of EndPoint
**  objects.  Each EndPoint has a file descriptor it is interested in.  The
**  users of the EndPoint tell the EndPoints to notify them when a read or
**  write has been completed (or simple if the file descriptor is read or
**  write ready).
*/

#include "portable/system.h"

#include "innfeed.h"
#include "portable/socket.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <syslog.h>

#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#include <time.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/messages.h"

#include "buffer.h"
#include "configfile.h"
#include "endpoint.h"
#include "host.h"

static const char *const timer_name[] = {"idle",   "blstats", "stsfile",
                                         "newart", "readart", "prepart",
                                         "read",   "write",   "cb"};

#if !defined(NSIG)
#    define NSIG 32
#endif


namespace asio = boost::asio;

/* This is the structure that is the EndPoint.  Each is owned by a
   shared_ptr in endPoints; a pending wait and the ready list hold more, so
   an endpoint deleted inside a callback (or with a wait pending) stays
   valid until those let go, and closed tells them it is gone. */
struct endpoint_s : std::enable_shared_from_this<endpoint_s> {
    /* fields for managing multiple reads into the inBuffer. */
    Buffer *inBuffer = nullptr;   /* list of buffers to read into */
    unsigned int inBufferIdx = 0; /* where is list we're at. */
    size_t inIndex = 0;           /* unused since doRead's fix */
    size_t inMinLen = 0;          /* minimum amount to read */
    size_t inAmtRead = 0;         /* amount read so far */
    EndpRWCB inCbk = nullptr;     /* callback for when read complete */
    void *inClientData = nullptr; /* callback data */

    /* fields for managing multiple writes from the outBuffer */
    Buffer *outBuffer = nullptr;     /* list of buffers to write */
    unsigned int outBufferIdx = 0;   /* index into buffer list to start write */
    size_t outIndex = 0;             /* where in current buffer we write from */
    size_t outSize = 0;              /* total of all the buffers */
    size_t outAmtWritten = 0;        /* amount written so far */
    EndpRWCB outProgressCbk = nullptr; /* callback when done */
    EndpRWCB outDoneCbk = nullptr;     /* callback when done */
    void *outClientData = nullptr;     /* callback data */

    EndpWorkCbk workCbk = nullptr; /* callback to run if no I/O to do */
    void *workData = nullptr;      /* data for callback */

    int myFd = -1;   /* the file descriptor we're handling */
    int myErrno = 0; /* the errno when I/O fails */

    /* The loop's view.  wantRead and wantWrite are what the select() loop's
       read and write sets held; readArmed and writeArmed say a wait is
       pending; readReady and writeReady are set by a completed wait and
       taken by the next pass of Run().  An endpoint that cannot be waited
       on (a regular file: epoll refuses them) has no stream and is ready
       whenever it wants to read or write, as select() reported it. */
    std::optional<asio::posix::stream_descriptor> stream;
    bool wantRead = false, wantWrite = false;
    bool readArmed = false, writeArmed = false;
    bool readReady = false, writeReady = false;
    bool queued = false;  /* in readyList */
    bool hitThisPass = false;
    bool closed = false;
};


/* A private structure. These hold the information on the timer callbacks. */
typedef struct timerqelem_s {
    TimeoutId id;              /* the id we gave out */
    time_t when;               /* The time the timer should go off */
    EndpTCB func;              /* the function to call */
    void *data;                /* the client callback data */
    struct timerqelem_s *next; /* next in the queue */
} *TimerElem, TimerElemStruct;

static EndPoint mainEndPoint;
unsigned int stdioFdMax = MAX_STDIO_FD;
time_t PrivateTime;

typedef void (*sigfn)(int);
static std::vector<sigfn> sigHandlers; /* indexed by signal number */


/* private functions */
static IoStatus doRead(EndPoint endp);
static IoStatus doWrite(EndPoint endp);
static void pipeHandler(int s);
static TimerElem newTimerElem(TimeoutId i, time_t w, EndpTCB f, void *d);
static TimeoutId timerElemAdd(time_t when, EndpTCB func, void *data);
static void doTimeout(void);
static void arm(EndPoint ep);

static void endpointCleanup(void);


/* Private data.  The io_context is declared before everything that refers
   to it, so it is destroyed after them at exit.  It is made on first use,
   after main() has forked its subprogram (kqueue does not survive a
   fork). */
static std::optional<asio::io_context> io;
static std::optional<asio::steady_timer> wakeTimer; /* the timer queue's head */
static std::optional<asio::signal_set> signals;     /* setSigHandler's */
static bool wakeArmed = false;  /* wakeTimer has a wait pending */
static time_t wakeWhen = 0;     /* ...for this head time */
static bool timerFired = false; /* the head's time has come */
static bool signalsArmed = false;

static std::vector<std::shared_ptr<endpoint_s>> endPoints; /* live ones */
static std::vector<std::shared_ptr<endpoint_s>> readyList; /* for next pass */

static int keepSelecting;

static TimerElem timeoutQueue;
static TimerElem timeoutPool;
static TimeoutId nextId;
static int timeoutQueueLength;


static asio::io_context &
ioc(void)
{
    if (!io) {
        io.emplace();
        wakeTimer.emplace(*io);
        signals.emplace(*io);
    }
    return *io;
}


/* Create a new EndPoint and hook it to the give file descriptor. All
   fields are initialized to appropriate values.  On the first time this
   function is called the global data structs that manages lists of
   endpoints are initialized. */
static bool inited = false;

EndPoint
newEndPoint(int fd)
{
    struct stat st;

    if (!inited) {
        inited = true;
        atexit(endpointCleanup);
    }

    if (fd < 0)
        return NULL;

    /* try to dup the fd to a larger number to leave lower values free for
       broken stdio implementations. */
    if (stdioFdMax > 0 && ((unsigned int) fd) <= stdioFdMax) {
        int newfd = fcntl(fd, F_DUPFD, stdioFdMax + 1);
        if (newfd >= 0) {
            d_printf(1, "Dupped fd %d to %d\n", fd, newfd);
            if (close(fd) != 0)
                syswarn("ME oserr close (%d)", fd);
        } else {
            d_printf(1, "Couldn't dup fd %d to above %u\n", fd, stdioFdMax);
            newfd = fd;
        }

        fd = newfd;
    }

    auto ep = std::make_shared<endpoint_s>();

    ep->myFd = fd;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        boost::system::error_code ec;

        ep->stream.emplace(ioc());
        ep->stream->assign(fd, ec);
        if (ec) {
            warn("ME cannot add fd %d to the event loop: %s", fd,
                 ec.message().c_str());
            ep->stream.reset();
        }
    }
    endPoints.push_back(ep);
    return ep.get();
}


/* Delete the given endpoint. The files descriptor is closed and the two
   Buffer arrays are released. */

void
delEndPoint(EndPoint ep)
{
    if (ep == NULL)
        return;

    auto it = std::find_if(endPoints.begin(), endPoints.end(),
                           [ep](const auto &p) { return p.get() == ep; });

    ASSERT(it != endPoints.end());
    auto self = *it;
    endPoints.erase(it);

    if (mainEndPoint == ep)
        mainEndPoint = NULL;

    if (ep->inBuffer != NULL)
        freeBufferArray(ep->inBuffer);

    if (ep->outBuffer != NULL)
        freeBufferArray(ep->outBuffer);
    ep->inBuffer = ep->outBuffer = NULL;

    /* Closing the stream closes the descriptor and cancels its waits. */
    ep->closed = true;
    ep->wantRead = ep->wantWrite = false;
    if (ep->stream) {
        boost::system::error_code ignored;

        ep->stream->close(ignored);
    } else
        close(ep->myFd);
}

int
endPointFd(EndPoint endp)
{
    ASSERT(endp != NULL);

    return endp->myFd;
}


/* Put an endpoint on the ready list for the next pass. */
static void
queueReady(const std::shared_ptr<endpoint_s> &ep)
{
    if (!ep->queued) {
        ep->queued = true;
        readyList.push_back(ep);
    }
}


/* Wait for the endpoint's descriptor to be readable or writable, if it
   wants that and no wait is pending.  A completed wait marks it ready for
   the next pass. */
static void
arm(EndPoint ep)
{
    if (ep->closed || !ep->stream)
        return;
    if (ep->wantRead && !ep->readArmed) {
        ep->readArmed = true;
        ep->stream->async_wait(
            asio::posix::stream_descriptor::wait_read,
            [self = ep->shared_from_this()](
                const boost::system::error_code &ec) {
                self->readArmed = false;
                if (ec == asio::error::operation_aborted || self->closed)
                    return;
                self->readReady = true;
                queueReady(self);
            });
    }
    if (ep->wantWrite && !ep->writeArmed) {
        ep->writeArmed = true;
        ep->stream->async_wait(
            asio::posix::stream_descriptor::wait_write,
            [self = ep->shared_from_this()](
                const boost::system::error_code &ec) {
                self->writeArmed = false;
                if (ec == asio::error::operation_aborted || self->closed)
                    return;
                self->writeReady = true;
                queueReady(self);
            });
    }
}


/* Request a read to be done next time there's data. The endpoint ENDP
 * is what will do the read. BUFFERS is the array of Buffers the data
 * should go into. FUNC is the callback function to call when the read
 * is complete. CLIENTDATA is the client data to pass back into the
 * callback function. MINLEN is the minimum amount of data to
 * read. If MINLEN is 0 then then BUFFERS must be filled, otherwise at
 * least MINLEN bytes must be read.
 *
 * BUFFERS can be NULL, in which case no read is actually done, but the
 * callback function will be called still. This is useful for
 * listening sockets.
 *
 * Returns 0 if the read couldn't be prepared (for example if a read
 * is already outstanding).
 */

int
prepareRead(EndPoint endp, Buffer *buffers, EndpRWCB func, void *clientData,
            int minlen)
{
    int bufferSizeTotal = 0;
    int idx;

    ASSERT(endp != NULL);

    if (endp->inBuffer != NULL || endp->wantRead)
        return 0; /* something already there */

    for (idx = 0; buffers != NULL && buffers[idx] != NULL; idx++) {
        size_t bs = bufferSize(buffers[idx]);
        size_t bds = bufferDataSize(buffers[idx]);

        bufferSizeTotal += (bs - bds);
    }

    endp->inBuffer = buffers;
    endp->inBufferIdx = 0;
    endp->inIndex = 0;
    endp->inMinLen = (minlen > 0 ? minlen : bufferSizeTotal);
    endp->inCbk = func;
    endp->inAmtRead = 0;
    endp->inClientData = clientData;

    endp->wantRead = true;
    arm(endp);

    return 1;
}


/* Request a write to be done at a later point. ENDP is the EndPoint to
 * do the write. BUFFERS is the array of Buffers to write from. FUNC is
 * the function to call when the write is complete. CLIENTDATA is some
 * data to hand back to the callback function.
 *
 * If BUFFERS is NULL, then no write will actually by done, but the
 * callback function will still be called. This is useful for
 * connecting sockets.
 *
 * Returns 0 if the write couldn't be prepared (like if a write is
 * still in process.
 */
int
prepareWrite(EndPoint endp, Buffer *buffers, EndpRWCB progress, EndpRWCB done,
             void *clientData)
{
    int bufferSizeTotal = 0;
    int idx;

    ASSERT(endp != NULL);

    if (endp->outBuffer != NULL || endp->wantWrite)
        return 0; /* something already there */

    for (idx = 0; buffers != NULL && buffers[idx] != NULL; idx++)
        bufferSizeTotal += bufferDataSize(buffers[idx]);

    endp->outBuffer = buffers;
    endp->outBufferIdx = 0;
    endp->outIndex = 0;
    endp->outProgressCbk = progress;
    endp->outDoneCbk = done;
    endp->outClientData = clientData;
    endp->outSize = bufferSizeTotal;
    endp->outAmtWritten = 0;

    endp->wantWrite = true;
    arm(endp);

    return 1;
}


/* queue up a new timeout request. to go off at a specific time. */
TimeoutId
prepareWake(EndpTCB func, time_t timeToWake, void *clientData)
{
    TimeoutId id;
    time_t now = theTime();

    if (now > timeToWake)
        return 0;

    id = timerElemAdd(timeToWake, func, clientData);

#if defined(INNFEED_DEBUG)
    d_printf(1, "Preparing wake %d at date %lu for %ld seconds\n", (int) id,
             (unsigned long) now, (long) (timeToWake - now));
#endif

    return id;
}


/* queue up a new timeout request to off TIMETOSLEEP seconds from now */
TimeoutId
prepareSleep(EndpTCB func, int timeToSleep, void *clientData)
{
    time_t now = theTime();
    TimeoutId id;

    id = timerElemAdd(now + timeToSleep, func, clientData);

#if defined(INNFEED_DEBUG)
    d_printf(1, "Preparing sleep %d at date %lu for %ld seconds\n", (int) id,
             (unsigned long) now, (long) timeToSleep);
#endif

    return id;
}


/* Updates an existing timeout request to go off TIMETOSLEEP seconds from
   now, or queues a new request.  Always returns a new ID. */
TimeoutId
updateSleep(TimeoutId tid, EndpTCB func, int timeToSleep, void *clientData)
{
    if (tid == 0)
        return prepareSleep(func, timeToSleep, clientData);
    else {
        /* XXX - quick and dirty but CPU wasteful implementation */
        removeTimeout(tid);
        return prepareSleep(func, timeToSleep, clientData);
    }
}


/* Remove a timeout that was previously prepared. 0 is a legal value that
   is just ignored. */
bool
removeTimeout(TimeoutId tid)
{
    TimerElem n = timeoutQueue;
    TimerElem p = NULL;

    if (tid == 0)
        return true;

    while (n != NULL && n->id != tid) {
        p = n;
        n = n->next;
    }

    if (n == NULL)
        return false;

    if (p == NULL) /* at the head. */
        timeoutQueue = n->next;
    else
        p->next = n->next;

    n->next = timeoutPool;
    timeoutPool = n;

    timeoutQueueLength--;

    return true;
}


/* Fire the timers whose time has passed, and point wakeTimer at the queue's
   head.  Returns false if the queue is empty; *now0 is set if the head is
   due in this second (select() was given a zero timeout for that). */
static bool
armWakeTimer(bool *now0)
{
    time_t now;

    *now0 = false;
    if (timeoutQueue != NULL) {
        now = theTime();
        while (timeoutQueue && now > timeoutQueue->when)
            doTimeout();
    }
    if (timeoutQueue == NULL) {
        if (wakeArmed)
            wakeTimer->cancel();
        wakeArmed = false;
        timerFired = false;
        return false;
    }
    now = theTime();
    *now0 = (timeoutQueue->when == now);
    if (!wakeArmed || wakeWhen != timeoutQueue->when) {
        timerFired = false;
        wakeWhen = timeoutQueue->when;
        wakeArmed = true;
        wakeTimer->expires_after(std::chrono::seconds(wakeWhen - now));
        wakeTimer->async_wait([](const boost::system::error_code &ec) {
            if (ec == asio::error::operation_aborted)
                return;
            wakeArmed = false;
            timerFired = true;
        });
    }
    return true;
}


/* The main routine. This is a near-infinite loop that drives the whole
   program.

   Each pass is the select() loop's (ASIO_PLAN.md, phase 3): fire overdue
   timers, sample the backlog statistics, wait (TMR_IDLE) until an endpoint
   is ready, a timer is due or a signal arrives -- or only poll, while a
   work callback is registered or an endpoint that cannot be waited on is
   ready -- then do the ready endpoints' reads and writes and their
   callbacks, the head timer if nothing else happened, and the work
   callbacks of the endpoints that were not ready.  Asio does the waiting;
   the endpoints are served in the order they became ready (the select()
   loop's priority order by readiness is gone, by the architect's
   decision). */
void
Run(void)
{
    keepSelecting = 1;
    xsignal(SIGPIPE, pipeHandler);
    ioc();

    while (keepSelecting) {
        bool now0, haveTimeout, work = false, poll, modifiedTime;
        IoStatus rval;

        haveTimeout = armWakeTimer(&now0);
        if (endPoints.empty() && !haveTimeout) /* no fds and no timeout */
            break;

        /* Endpoints that cannot be waited on are always ready. */
        for (auto &ep : endPoints)
            if (!ep->stream && (ep->wantRead || ep->wantWrite)) {
                ep->readReady = ep->wantRead;
                ep->writeReady = ep->wantWrite;
                queueReady(ep);
            }

        /* if we have any workprocs registered we poll rather than block on
           the fds */
        for (auto &ep : endPoints)
            if (ep->workCbk != NULL) {
                work = true;
                break;
            }
        modifiedTime = work && haveTimeout && !now0;
        poll = work || !readyList.empty();

        /* calculate host backlog statistics */
        TMRstart(TMR_BACKLOGSTATS);
        gCalcHostBlStat();
        TMRstop(TMR_BACKLOGSTATS);

        TMRstart(TMR_IDLE);
        io->restart();
        if (poll)
            io->poll();
        else if (io->run_one() == 0) {
            /* Nothing to wait for: no wait, timer or signal is pending, so
               nothing can happen (select() would have blocked forever). */
            TMRstop(TMR_IDLE);
            break;
        } else
            io->poll();
        TMRstop(TMR_IDLE);

        timePasses();
        if (innconf->timer != 0 && TMRnow() > innconf->timer * 1000) {
            TMRsummary("ME", timer_name);
        }

        std::vector<std::shared_ptr<endpoint_s>> ready;
        ready.swap(readyList);
        for (auto &ep : ready) {
            bool readReady = ep->readReady, writeReady = ep->writeReady;

            ep->queued = false;
            ep->readReady = ep->writeReady = false;
            ep->hitThisPass = true;
            if (ep->closed)
                continue;

            if (readReady && ep->wantRead) {
                if ((rval = doRead(ep.get())) != IoIncomplete) {
                    Buffer *buff = ep->inBuffer;

                    ep->wantRead = false;

                    /* in case callback wants to issue read */
                    ep->inBuffer = NULL;

                    if (ep->inCbk != NULL)
                        (*ep->inCbk)(ep.get(), rval, buff, ep->inClientData);
                    else
                        freeBufferArray(buff);
                }
            }

            /* the read callback may have deleted the endpoint */
            if (!ep->closed && writeReady && ep->wantWrite) {
                if ((rval = doWrite(ep.get())) != IoIncomplete
                    && rval != IoProgress) {
                    Buffer *buff = ep->outBuffer;

                    ep->wantWrite = false;

                    /* in case callback wants to issue a write */
                    ep->outBuffer = NULL;

                    if (ep->outDoneCbk != NULL)
                        (*ep->outDoneCbk)(ep.get(), rval, buff,
                                          ep->outClientData);
                    else
                        freeBufferArray(buff);
                } else if (rval == IoProgress) {
                    Buffer *buff = ep->outBuffer;

                    if (ep->outProgressCbk != NULL)
                        (*ep->outProgressCbk)(ep.get(), rval, buff,
                                              ep->outClientData);
                }
            }

            if (!ep->closed)
                arm(ep.get());
        }

        if (ready.empty() && timerFired && !modifiedTime) {
            timerFired = false;
            doTimeout();
        }

        /* now we're done processing all read fds and/or the
           timeout(s). Next we do the work callbacks for all the endpoints
           whose fds weren't ready. */
        if (work) {
            std::vector<std::shared_ptr<endpoint_s>> all(endPoints);

            for (auto &ep : all)
                if (!ep->closed && !ep->hitThisPass && ep->workCbk != NULL) {
                    EndpWorkCbk func = ep->workCbk;
                    void *data = ep->workData;

                    ep->workCbk = NULL;
                    ep->workData = NULL;
                    TMRstart(TMR_CALLBACK);
                    func(ep.get(), data);
                    TMRstop(TMR_CALLBACK);
                }
        }
        for (auto &ep : ready)
            ep->hitThisPass = false;
    }
}

void *
addWorkCallback(EndPoint endp, EndpWorkCbk cbk, void *data)
{
    void *oldBk = endp->workData;

    endp->workCbk = cbk;
    endp->workData = data;

    return oldBk;
}

/* Tell the Run routine to stop next time around. */
void
stopRun(void)
{
    keepSelecting = 0;
}


int
endPointErrno(EndPoint endp)
{
    return endp->myErrno;
}

bool
readIsPending(EndPoint endp)
{
    return (endp->inBuffer != NULL ? true : false);
}

bool
writeIsPending(EndPoint endp)
{
    return (endp->outBuffer != NULL ? true : false);
}

void
setMainEndPoint(EndPoint endp)
{
    struct stat buf;

    mainEndPoint = endp;
    if (endp->myFd >= 0 && fstat(endp->myFd, &buf) < 0)
        syslog(LOG_ERR, "Can't fstat mainEndPoint fd (%d): %m", endp->myFd);
    else if (endp->myFd < 0)
        syslog(LOG_ERR, "Negative fd for mainEndPoint???");
}

int
getMainEndPointFd(void)
{
    return (mainEndPoint->myFd);
}

void
freeTimeoutQueue(void)
{
    TimerElem p, n;

    p = timeoutQueue;
    while (p) {
        n = p->next;
        p->next = timeoutPool;
        timeoutPool = p;
        p = n;
        timeoutQueueLength--;
    }
}


/***********************************************************************/
/*                      STATIC FUNCTIONS BELOW HERE                    */
/***********************************************************************/


/*
 * called when the file descriptor on this endpoint is read ready.
 */
static IoStatus
doRead(EndPoint endp)
{
    int i = 0;
    unsigned int idx;
    unsigned int bCount = 0;
    struct iovec *vp = NULL;
    Buffer *buffers = endp->inBuffer;
    unsigned int currIdx = endp->inBufferIdx;
    IoStatus rval = IoIncomplete;

    TMRstart(TMR_READ);
    for (i = currIdx; buffers && buffers[i] != NULL; i++)
        bCount++;

    bCount = (bCount > IOV_MAX ? IOV_MAX : bCount);

    /* now set up the iovecs for the readv: each buffer from inBufferIdx on,
       from the end of the data it already holds (its data size is the
       read position; a buffer is left when it is full). */
    if (bCount > 0) {
        vp = xcalloc(bCount, sizeof(struct iovec));

        for (idx = 0; idx < bCount; idx++) {
            Buffer buf = buffers[currIdx + idx];
            size_t bds = bufferDataSize(buf);

            vp[idx].iov_base = static_cast<char *>(bufferBase(buf)) + bds;
            vp[idx].iov_len = bufferSize(buf) - bds;
        }

        i = readv(endp->myFd, vp, (int) bCount);

        if (i > 0) {
            size_t readAmt = (size_t) i;

            endp->inAmtRead += readAmt;

            /* Add what was read to each buffer in turn. */
            for (idx = 0; readAmt > 0 && idx < bCount; idx++) {
                size_t take = std::min(readAmt, (size_t) vp[idx].iov_len);

                bufferIncrDataSize(buffers[currIdx + idx], take);
                readAmt -= take;
                if (take == vp[idx].iov_len)
                    endp->inBufferIdx++;
            }

            if (endp->inAmtRead >= endp->inMinLen)
                rval = IoDone;
        } else if (i < 0 && errno != EINTR && errno != EAGAIN) {
            endp->myErrno = errno;
            rval = IoFailed;
        } else if (i < 0 && errno == EINTR) {
            /* incomplete: tried again on the next pass */
        } else if (i == 0)
            rval = IoEOF;
        else /* i < 0 && errno == EAGAIN */
            rval = IoIncomplete;

        free(vp);
    } else
        rval = IoDone;
    TMRstop(TMR_READ);
    return rval;
}

/* called when the file descriptor on the endpoint is write ready. */
static IoStatus
doWrite(EndPoint endp)
{
    unsigned int idx;
    int i = 0;
    unsigned int bCount = 0;
    struct iovec *vp = NULL;
    Buffer *buffers = endp->outBuffer;
    unsigned int currIdx = endp->outBufferIdx;
    IoStatus rval = IoIncomplete;

    TMRstart(TMR_WRITE);
    for (i = currIdx; buffers && buffers[i] != NULL; i++)
        bCount++;

    bCount = (bCount > IOV_MAX ? IOV_MAX : bCount);

    if (bCount > 0) {
        vp = xcalloc(bCount, sizeof(struct iovec));

        vp[0].iov_base = bufferBase(buffers[currIdx]);
        vp[0].iov_base = (char *) vp[0].iov_base + endp->outIndex;
        vp[0].iov_len = bufferDataSize(buffers[currIdx]) - endp->outIndex;

        for (idx = 1; idx < bCount; idx++) {
            vp[idx].iov_base = bufferBase(buffers[idx + currIdx]);
            vp[idx].iov_len = bufferDataSize(buffers[idx + currIdx]);
        }

        ASSERT(endp->myFd >= 0);
        ASSERT(vp != 0);
        ASSERT(bCount > 0);

        i = writev(endp->myFd, vp, (int) bCount);

        if (i > 0) {
            size_t writeAmt = (size_t) i;

            endp->outAmtWritten += writeAmt;

            /* now figure out which buffers got completely written */
            for (idx = 0; writeAmt > 0; idx++) {
                if (writeAmt >= (size_t) vp[idx].iov_len) {
                    endp->outBufferIdx++;
                    endp->outIndex = 0;
                    writeAmt -= vp[idx].iov_len;
                } else {
                    /* this buffer was not completely written */
                    endp->outIndex += writeAmt;
                    writeAmt = 0;
                }
            }

            if (endp->outAmtWritten == endp->outSize)
                rval = IoDone;
            else
                rval = IoProgress;
        } else if (i < 0 && errno == EINTR) {
            /* incomplete: tried again on the next pass */
        } else if (i < 0 && errno == EAGAIN) {
            rval = IoIncomplete;
        } else if (i < 0) {
            endp->myErrno = errno;
            rval = IoFailed;
        } else
            d_printf(1, "Wrote 0 bytes in doWrite()?\n");

        free(vp);
    } else
        rval = IoDone;

    TMRstop(TMR_WRITE);
    return rval;
}


static void
pipeHandler(int s)
{
    xsignal(s, pipeHandler);
}


#define TIMEOUT_POOL_SIZE \
    ((4096 - 2 * (sizeof(void *))) / (sizeof(TimerElemStruct)))

/* create a new timeout data structure properly initialized. */
static TimerElem
newTimerElem(TimeoutId i, time_t w, EndpTCB f, void *d)
{
    TimerElem p;

    if (timeoutPool == NULL) {
        unsigned int j;

        timeoutPool = xmalloc(sizeof(TimerElemStruct) * TIMEOUT_POOL_SIZE);

        for (j = 0; j < TIMEOUT_POOL_SIZE - 1; j++)
            timeoutPool[j].next = &(timeoutPool[j + 1]);
        timeoutPool[TIMEOUT_POOL_SIZE - 1].next = NULL;
    }

    p = timeoutPool;
    timeoutPool = timeoutPool->next;

    ASSERT(p != NULL);

    p->id = i;
    p->when = w;
    p->func = f;
    p->data = d;
    p->next = NULL;

    return p;
}


/* add a new timeout structure to the global list. */
static TimeoutId
timerElemAdd(time_t when, EndpTCB func, void *data)
{
    TimerElem p = newTimerElem(++nextId ? nextId : ++nextId, when, func, data);
    TimerElem n = timeoutQueue;
    TimerElem q = NULL;

    while (n != NULL && n->when <= when) {
        q = n;
        n = n->next;
    }

    if (n == NULL && q == NULL) /* empty list so put at head */
        timeoutQueue = p;
    else if (q == NULL) /* put at head of list */
    {
        p->next = timeoutQueue;
        timeoutQueue = p;
    } else if (n == NULL) /* put at end of list */
        q->next = p;
    else /* in middle of list */
    {
        p->next = q->next;
        q->next = p;
    }

    timeoutQueueLength++;

    return p->id;
}


static void
doTimeout(void)
{
    EndpTCB cbk = timeoutQueue->func;
    void *data = timeoutQueue->data;
    TimerElem p = timeoutQueue;
    TimeoutId tid = timeoutQueue->id;

    timeoutQueue = timeoutQueue->next;

    p->next = timeoutPool;
    timeoutPool = p;

    timeoutQueueLength--;

    if (cbk)
        (*cbk)(tid, data); /* call the callback function */
}


#if defined(WANT_MAIN)

#    define BUFF_SIZE 100

static void
lineIsWritten(EndPoint ep, IoStatus status, Buffer *buffers, void *d UNUSED)
{
    int i;

    if (status == IoDone)
        d_printf(1, "LINE was written\n");
    else {
        int oldErrno = errno;

        errno = endPointErrno(ep);
        perror("write failed");
        errno = oldErrno;
    }

    for (i = 0; buffers && buffers[i]; i++) {
        delBuffer(buffers[i]);
    }
}

static void
lineIsRead(EndPoint myEp, IoStatus status, Buffer *buffers, void *d UNUSED)
{
    Buffer *writeBuffers, *readBuffers;
    Buffer newBuff1, newBuff2;
    Buffer newInputBuffer;
    char *data, *p;
    size_t len;

    if (status == IoFailed) {
        int oldErrno = errno;

        errno = endPointErrno(myEp);
        perror("read failed");
        errno = oldErrno;

        return;
    } else if (status == IoEOF) {
        d_printf(1, "EOF on endpoint.\n");
        delEndPoint(myEp);

        return;
    }


    data = bufferBase(buffers[0]);
    len = bufferDataSize(buffers[0]);

    if (data[len - 1] == '\r' || data[len - 1] == '\n')
        bufferDecrDataSize(buffers[0], 1);
    if (data[len - 1] == '\r' || data[len - 1] == '\n')
        bufferDecrDataSize(buffers[0], 1);

    data[len] = '\0';

    d_printf(1, "Got a line: %s\n", data);

    newBuff1 = newBuffer(len + 50);
    newBuff2 = newBuffer(len + 50);
    newInputBuffer = newBuffer(BUFF_SIZE);

    p = bufferBase(newBuff1);
    strlcpy(p, "Thanks for that \"", bufferSize(newBuff1));
    bufferSetDataSize(newBuff1, strlen(p));

    p = bufferBase(newBuff2);
    strlcpy(p, "\" very tasty\n", bufferSize(newBuff2));
    bufferSetDataSize(newBuff2, strlen(p));

    writeBuffers = makeBufferArray(newBuff1, buffers[0], newBuff2, NULL);
    readBuffers = makeBufferArray(newInputBuffer, NULL);

    prepareWrite(myEp, writeBuffers, NULL, lineIsWritten, NULL);
    prepareRead(myEp, readBuffers, lineIsRead, NULL, 1);
}


static void
printDate(TimeoutId tid, void *data)
{
    char dateString[30];
    const time_t t = theTime();

    timeToString(t, dateString, sizeof(dateString));
    d_printf(1, "Timeout (%d) time now is %lu %s\n", (int) tid,
             (unsigned long) t, dateString);

    if (timeoutQueue == NULL) {
        int ti = (rand() % 10) + 1;

        prepareSleep(printDate, ti, data);
    }
}

TimeoutId rm;

static void
Timeout(TimeoutId tid, void *data)
{
    static int seeded;
    static int howMany;
    static int i;
    char dateString[30];
    const time_t t = theTime();

    if (!seeded) {
        srand(t);
        seeded = 1;
    }

    timeToString(t, dateString, sizeof(dateString));
    d_printf(1, "Timeout (%d) time now is %lu %s\n", (int) tid,
             (unsigned long) t, dateString);

    if (timeoutQueue != NULL && timeoutQueue->next != NULL)
        d_printf(1, "%s timeout id %d\n",
                 (removeTimeout(rm) ? "REMOVED" : "FAILED TO REMOVE"), rm);
    rm = 0;

    howMany = (rand() % 10) + (timeoutQueue == NULL ? 1 : 0);

    for (i = 0; i < howMany; i++) {
        TimeoutId id;
        int count = (rand() % 30) + 1;

        id = (i % 2 == 0 ? prepareSleep(Timeout, count, data)
                         : prepareWake(Timeout, t + count, data));

        if (rm == 0)
            rm = id;
    }
}


static void
newConn(EndPoint ep, IoStatus status UNUSED, Buffer *buffers UNUSED,
        void *d UNUSED)
{
    EndPoint newEp;
    struct sockaddr_in in;
    Buffer *readBuffers;
    Buffer newBuff = newBuffer(BUFF_SIZE);
    socklen_t len = sizeof(in);
    int fd;

    memset(&in, 0, sizeof(in));

    fd = accept(ep->myFd, (struct sockaddr *) &in, &len);

    if (fd < 0) {
        perror("::accept");
        return;
    }

    newEp = newEndPoint(fd);

    prepareRead(ep, NULL, newConn, NULL, 0);

    readBuffers = makeBufferArray(newBuff, NULL);

    prepareRead(newEp, readBuffers, lineIsRead, NULL, 1);

    d_printf(1, "Set up a new connection\n");
}


int
main(int argc, char **argv)
{
    EndPoint accConn;
    struct sockaddr_in accNet;
    int accFd = socket(AF_INET, SOCK_STREAM, 0);
    unsigned short port = atoi(argc > 1 ? argv[1] : "10000");
    time_t t = theTime();


    char *program = strrchr(argv[0], '/');

    if (!program)
        program = argv[0];
    else
        program++;

    ASSERT(accFd >= 0);

    memset(&accNet, 0, sizeof(accNet));
    accNet.sin_family = AF_INET;
    accNet.sin_addr.s_addr = htonl(INADDR_ANY);
    accNet.sin_port = htons(port);

#    ifdef LOG_PERROR
    openlog(program, LOG_PERROR | LOG_PID, LOG_NEWS);
#    else
    openlog(program, LOG_PID, LOG_NEWS);
#    endif

    if (bind(accFd, (struct sockaddr *) &accNet, sizeof(accNet)) < 0) {
        perror("bind: %m");
        exit(1);
    }

    listen(accFd, 5);

    accConn = newEndPoint(accFd);

    prepareRead(accConn, NULL, newConn, NULL, 0);

    prepareSleep(Timeout, 5, (void *) 0x10);

    t = theTime();
    d_printf(1, "Time now is %s", ctime(&t));

    prepareWake(printDate, t + 16, NULL);

    Run();

    return 0;
}
#endif /* WANT_MAIN */

/* Wait for the next of setSigHandler's signals; its handler runs from the
   loop, as handleSignals() ran it after select() returned. */
static void
armSignals(void)
{
    if (signalsArmed)
        return;
    signalsArmed = true;
    signals->async_wait([](const boost::system::error_code &ec, int signo) {
        signalsArmed = false;
        if (ec)
            return;
        if (signo > 0 && (size_t) signo < sigHandlers.size()) {
            sigfn h = sigHandlers[signo];

            if (h != NULL && h != SIG_IGN && h != SIG_DFL)
                h(signo);
        }
        armSignals();
    });
}

/* Probably doesn't do the right thing for SIGCHLD */
void
setSigHandler(int signum, void (*ptr)(int))
{
    boost::system::error_code ec;

    if (signum >= NSIG) {
        syslog(LOG_ERR, "ME signal number bigger than NSIG: %d vs %d", signum,
               NSIG);
        return;
    }
    if (sigHandlers.empty())
        sigHandlers.assign(NSIG, nullptr);

    ioc();
    signals->add(signum, ec);
    if (ec)
        die("signal failed: %s", ec.message().c_str());

    sigHandlers[signum] = ptr;
    armSignals();
}


int
endpointConfigLoadCbk(FILE *fp)
{
    long ival;
    int rval = 1;

    if (getInteger(topScope, "stdio-fdmax", &ival, NO_INHERIT)) {
        stdioFdMax = ival;

#if !defined(FD_SETSIZE)

        if (stdioFdMax > 0) {
            logOrPrint(LOG_ERR, fp,
                       "ME config: cannot define stdio-fdmax if FD_SETSIZE is "
                       "not available");
            stdioFdMax = 0;
            rval = 0;
        }

#else

        if (stdioFdMax > FD_SETSIZE) {
            logOrPrint(LOG_ERR, fp,
                       "ME config: value of %s (%ld) in %s is higher"
                       " than maximum of %ld. Using %ld",
                       "stdio-fdmax", ival, "global scope", (long) FD_SETSIZE,
                       (long) FD_SETSIZE);
            stdioFdMax = FD_SETSIZE;
            rval = 0;
        }

#endif

    } else
        stdioFdMax = 0;

    return rval;
}


static void
endpointCleanup(void)
{
    readyList.clear();
    endPoints.clear();
    sigHandlers.clear();
}
