/*
**  The public interface to the Endpoint class.
**
**  Written by James Brister <brister@vix.com>
**
**  The EndPoint objects are encapsulations of file descriptors that normally
**  do blocking I/O (i.e. NOT fd's hooked to a disk file).  The EndPoint class
**  provides methods for requesting read/writes to happen when next possible
**  and for the requester to be notified when the I/O is complete (or failed
**  for some reason).  Facilities for timeout notifications are provided too.
**
**  We should add a way to cancel prepared read/writes.
*/

#ifndef ENDPOINT_H
#define ENDPOINT_H

#include "misc.h"

#include <ctime>

BEGIN_DECLS


#define clearTimer(timerId)         \
    do {                            \
        if ((timerId) != 0) {       \
            removeTimeout(timerId); \
            timerId = 0;            \
        }                           \
    } while (0)


/* Variables and macros to export. */

/* kre's cool idea for reducing the number of times time() is called. */
extern time_t PrivateTime;
#define theTime()    (PrivateTime ? PrivateTime : time(&PrivateTime))
#define timePasses() (PrivateTime = 0)

extern unsigned int stdioFdMax;


/* These typedefs really live in misc.h.
 *
 *****************************************
 *
 * The basic (opaque to the outside world) type:
 *
 *      typedef struct endpoint_s *EndPoint ;
 *
 *****************************************
 *
 * The return status of an I/O request:
 *
 *      typedef enum {
 *          IoDone,                     I/O completed successfully
 *          IoIncomplete,               I/O still got more to read/write
 *          IoProgress,                 I/O still got more to read/write
 *          IoFailed                    I/O failed
 *      } IoStatus ;
 *
 * The completion callbacks are never called with the status IoIncomplete or
 * IoProgress.
 *
 *****************************************
 *
 * typedef for function callback when I/O is complete (or failed).
 *      E is the EndPoint
 *      I is the status of the IO
 *      B is the buffer the IO was to read to or write from
 *      D is the client data originally given to prepare{Write,Read}
 *
 * typedef void (*EndpRWCB) (EndPoint e, IoStatus i, Buffer b, void *d) ;
 *
 *****************************************
 *
 * typedef for function callback when a timer has gone off.  D is the client
 * data given to prepare{Sleep,Wake}:
 *
 * typedef void (*EndpTCB) (void *d) ;
 *
 */

/* create a new EndPoint hooked up to the given file descriptor */
EndPoint newEndPoint(int fd);

/* shutdown the file descriptor and delete the endpoint. */
void delEndPoint(EndPoint ep);

/* return the file descriptor the endpoint is managing */
int endPointFd(EndPoint endp);

/* Request a read when available. Reads MINLEN bytes into the
 * buffers in BUFFERS. BUFFERS is an array of Buffers, the last of which
 * must be NULL. Note that ownership of BUFFERS is never asserted, but
 * the ownership of the Buffers in it is. So if an EndPoint is
 * deleted while a read is pending the Buffers will be released, but
 * the array won't be. If MINLEN is 0 then the buffers must be
 * filled. The FUNC function gets called when the read is
 * complete. CLIENTDATA is simply passed back to the
 * callback. Returns non-zero if can be scheduled for processing.
 */
int prepareRead(EndPoint endp, Buffer *buffers, EndpRWCB func,
                void *clientData, int minlen);

/* Request a write when possible. All the data in the buffers in
 * BUFFERS will be written out the endpoint. BUFFERS is a NULL
 * terminated array of Buffers. See prepareRead for a discussion on
 * the ownership of BUFFERS and the Buffers inside BUFFERS. The PROGRESS
 * callback function will be called and the CLIENTDATA value will be
 * passed through to it whenever any data is written except for the
 * final write.  The DONE callback function will be called and the
 * CLIENTDATA value will be passed through to it after the final write.
 * Returns non-zero if scheduled successfully.
 */
int prepareWrite(EndPoint endp, Buffer *buffers, EndpRWCB progress,
                 EndpRWCB done, void *clientData);

/* return true if prepareRead has been called, but not serviced yet */
bool readIsPending(EndPoint endp);

/* Request a wakeup at a given time. */
TimeoutId prepareWake(EndpTCB func, time_t timeToWake, void *clientData);

/* return true if prepareWrite has been called, but not serviced yet */
bool writeIsPending(EndPoint endp);

/* Requests a wakeup TIMETOSLEEP seconds from now. */
TimeoutId prepareSleep(EndpTCB func, int timeToSleep, void *clientData);

/* Updates tid to wakeup TIMETOSLEEP seconds from now. */
TimeoutId updateSleep(TimeoutId tid, EndpTCB func, int timeToSleep,
                      void *clientData);

/* Set up a function to be called whenever the endpoint's file descriptor
   is NOT ready. This is called after all other fd-ready endpoints have
   been serviced. */
void *addWorkCallback(EndPoint endp, EndpWorkCbk cbk, void *data);

void setSigHandler(int signum, void (*ptr)(int));

/* remove the timeout that was previously requested. Returns true if
   successfully removed, false otherwise. 0 is a legal parameter value, in
   which case the function simply returns. */
bool removeTimeout(TimeoutId tid);

/* start the select loop. An initial prepare(Read|Write) or a timeout
   better have been setup. Doesn't return unless stopRun called */
void Run(void);

/* stops the Run loop and makes Run() return */
void stopRun(void);

/* returns the errno the endpoint got. */
int endPointErrno(EndPoint endp);

/* Tell the EndPoint class that the given EndPoint should always be
   considered first for servicing (i.e. the EndPoint connectied to innd) */
void setMainEndPoint(EndPoint endp);

/* returns the fd of the main endpoint */
int getMainEndPointFd(void);

void freeTimeoutQueue(void);

int endpointConfigLoadCbk(FILE *fp);

END_DECLS

/* Typed registration (C++).  The functions above hand their callback the
   client data as void *, so each callback converted it back with a cast.
   These templates take the callback as a template argument and the client
   data as a typed pointer; the callback is declared with its own type
   (Connection, Host, ...), and a callback that does not match the data
   does not compile.  A small trampoline does the one conversion from the
   untyped API.  For example:

       static void getBanner(EndPoint, IoStatus, Buffer *, Connection);
       prepareRead<getBanner>(e, readBuffers, cxn, 1);
*/
#include <type_traits>

namespace endpoint_detail {
template <auto F, typename T>
void
rw(EndPoint e, IoStatus i, Buffer *b, void *d)
{
    F(e, i, b, static_cast<T *>(d));
}
template <auto F, typename T>
void
timer(TimeoutId id, void *d)
{
    F(id, static_cast<T *>(d));
}
template <auto F>
void
timer_nodata(TimeoutId id, void *)
{
    F(id);
}
template <auto F, typename T>
void
work(EndPoint e, void *d)
{
    F(e, static_cast<T *>(d));
}
template <auto F, typename T>
inline constexpr bool is_rw =
    std::is_invocable_r_v<void, decltype(F), EndPoint, IoStatus, Buffer *,
                          T *>;
template <auto F, typename T>
inline constexpr bool is_timer =
    std::is_invocable_r_v<void, decltype(F), TimeoutId, T *>;
} // namespace endpoint_detail

/* The typed form of an EndpRWCB for data of type T, for code that passes a
   read/write callback on to prepareWrite at run time. */
template <auto F, typename T>
    requires endpoint_detail::is_rw<F, T>
inline constexpr EndpRWCB endpRW = &endpoint_detail::rw<F, T>;

template <auto F, typename T>
    requires endpoint_detail::is_rw<F, T>
int
prepareRead(EndPoint endp, Buffer *buffers, T *clientData, int minlen)
{
    return prepareRead(endp, buffers, &endpoint_detail::rw<F, T>, clientData,
                       minlen);
}

/* A write with only a completion callback (no progress callback). */
template <auto Done, typename T>
    requires endpoint_detail::is_rw<Done, T>
int
prepareWrite(EndPoint endp, Buffer *buffers, T *clientData)
{
    return prepareWrite(endp, buffers, nullptr, &endpoint_detail::rw<Done, T>,
                        clientData);
}

template <auto F, typename T>
    requires endpoint_detail::is_timer<F, T>
TimeoutId
prepareSleep(int timeToSleep, T *clientData)
{
    return prepareSleep(&endpoint_detail::timer<F, T>, timeToSleep,
                        clientData);
}

/* A timer whose callback needs no client data: F takes the TimeoutId only. */
template <auto F>
    requires std::is_invocable_r_v<void, decltype(F), TimeoutId>
TimeoutId
prepareSleep(int timeToSleep)
{
    return prepareSleep(&endpoint_detail::timer_nodata<F>, timeToSleep,
                        nullptr);
}

template <auto F, typename T>
    requires endpoint_detail::is_timer<F, T>
TimeoutId
updateSleep(TimeoutId tid, int timeToSleep, T *clientData)
{
    return updateSleep(tid, &endpoint_detail::timer<F, T>, timeToSleep,
                       clientData);
}

template <auto F, typename T>
    requires std::is_invocable_r_v<void, decltype(F), EndPoint, T *>
void *
addWorkCallback(EndPoint endp, T *data)
{
    return addWorkCallback(endp, &endpoint_detail::work<F, T>, data);
}

#endif /* ENDPOINT_H */
