/*
**  I/O channel (and buffer) processing.
**
**  This file is the heart of innd.  Everything innd does is represented by
**  channels; waiting for connections, reading from network connections,
**  writing to network connections, writing to processes, and writing to files
**  are all channel operations.
**
**  Channels can be in one of three states: reading, writing, or sleeping.
**  A reading or writing channel waits on its descriptor through an Asio
**  io_context; a sleeping one sits there until its wake time or until
**  something else wakes it up.  CHANreadloop is the main I/O loop for innd:
**  each pass waits for readiness, then dispatches control to whatever
**  channels have work to do, at most one read and one write per channel.
*/

#include "portable/system.h"

#include <sys/stat.h>

#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include "inn/fdflag.h"
#include "inn/innconf.h"
#include "inn/network.h"
#include "innd.h"
#include "article.h"

/* These errno values don't exist on all systems, but may be returned as an
   (ignorable) error to setting the accept socket nonblocking.  Define them
   to 0 if they don't exist so that we can unconditionally compare errno to
   them in the code. */
#ifndef ENOTSOCK
#    define ENOTSOCK 0
#endif
#ifndef ENOTTY
#    define ENOTTY 0
#endif

static const char *const timer_name[] = {
    "idle", "artclean", "artwrite", "artcncl",  "sitesend", "overv",
    "perl", "python",   "nntpread", "artparse", "artlog",   "datamove"};

namespace asio = boost::asio;

/* Asio state for one descriptor, indexed by descriptor number.  Wait
   handlers hold a shared_ptr, so state replaced or dropped while a wait is
   pending is only written to, never used again.  A regular file cannot be
   waited on (epoll refuses it); select() always reported one ready, so it
   is treated as ready whenever it is wanted. */
struct chan_io {
    std::optional<asio::posix::stream_descriptor> stream;
    bool waitable = false;
    bool want_read = false;
    bool want_write = false;
    bool sleeping = false;
    bool read_armed = false;
    bool write_armed = false;
    bool read_ready = false;
    bool write_ready = false;
};

/* Global data about the channels. */
struct channels {
    std::vector<std::shared_ptr<chan_io>> io; /* Indexed by descriptor. */
    int sleep_count;  /* Number of sleeping channels. */
    int max_fd;       /* Max fd in read_set or write_set. */
    int max_sleep_fd; /* Max fd in sleep_set. */
    int table_size;   /* Total number of channels. */
    CHANNEL *table;   /* Table of channel structs. */

    /* Special prioritized channels, for the control and remconn channels.  We
       check these first each time. */
    CHANNEL **prioritized;
    int prioritized_size;
};

/* The io_context is created on first use, after innd has become a daemon
   (a kqueue does not survive fork), and declared before everything that
   holds Asio objects so that it is destroyed after them. */
static std::optional<asio::io_context> io;
static std::optional<asio::steady_timer> wait_timer;
static std::optional<asio::signal_set> signals;
static std::vector<void (*)(int)> signal_handlers(NSIG, nullptr);

/* Eventually this will move into some sort of global INN state structure. */
static struct channels channels;

static asio::io_context &
ioc()
{
    if (!io) {
        io.emplace(1);
        wait_timer.emplace(*io);
    }
    return *io;
}

/* The state for a descriptor, or nullptr if out of range. */
static chan_io *
chan_state(int fd)
{
    if (fd < 0 || static_cast<size_t>(fd) >= channels.io.size())
        return nullptr;
    if (!channels.io[fd])
        channels.io[fd] = std::make_shared<chan_io>();
    return channels.io[fd].get();
}

static bool
wants_read(int fd)
{
    chan_io *s = chan_state(fd);
    return s != nullptr && s->want_read;
}

static bool
wants_write(int fd)
{
    chan_io *s = chan_state(fd);
    return s != nullptr && s->want_write;
}

static bool
is_sleeping(int fd)
{
    chan_io *s = chan_state(fd);
    return s != nullptr && s->sleeping;
}

/* Drop the Asio registration of a descriptor before it is closed or
   replaced, so that a later descriptor with the same number starts clean.
   The descriptor itself is left open. */
void
CHANdropfd(int fd)
{
    if (fd < 0 || static_cast<size_t>(fd) >= channels.io.size()
        || !channels.io[fd])
        return;
    chan_io &s = *channels.io[fd];
    if (s.stream) {
        boost::system::error_code ec;
        s.stream->cancel(ec);
        s.stream->release();
    }
    channels.io[fd].reset();
}

/* Give a descriptor a stream_descriptor if it can be waited on. */
static void
CHANattach(int fd)
{
    chan_io *s = chan_state(fd);
    struct stat st;

    if (s == nullptr || s->stream)
        return;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
        return;
    boost::system::error_code ec;
    asio::posix::stream_descriptor sd(ioc());
    sd.assign(fd, ec);
    if (ec)
        return;
    s->stream.emplace(std::move(sd));
    s->waitable = true;
}

/* Start a wait for each direction the descriptor wants and is not already
   waiting or ready for. */
static void
CHANarm(int fd)
{
    auto &slot = channels.io[fd];
    if (!slot)
        return;
    std::shared_ptr<chan_io> s = slot;
    if (!s->waitable) {
        if (s->want_read)
            s->read_ready = true;
        if (s->want_write)
            s->write_ready = true;
        return;
    }
    if (s->want_read && !s->read_armed && !s->read_ready) {
        s->read_armed = true;
        s->stream->async_wait(asio::posix::stream_descriptor::wait_read,
                              [s](const boost::system::error_code &ec) {
                                  s->read_armed = false;
                                  if (!ec && s->want_read)
                                      s->read_ready = true;
                              });
    }
    if (s->want_write && !s->write_armed && !s->write_ready) {
        s->write_armed = true;
        s->stream->async_wait(asio::posix::stream_descriptor::wait_write,
                              [s](const boost::system::error_code &ec) {
                                  s->write_armed = false;
                                  if (!ec && s->want_write)
                                      s->write_ready = true;
                              });
    }
}


/*
**  Run a signal handler from the main loop when the signal arrives.  This
**  replaces xsignal for innd: the handler runs between passes, never in
**  the middle of other code, so nothing needs masking.
*/
static void
CHANarmsignals()
{
    signals->async_wait([](const boost::system::error_code &ec, int sig) {
        if (ec)
            return;
        if (sig > 0 && sig < NSIG && signal_handlers[sig] != nullptr)
            signal_handlers[sig](sig);
        CHANarmsignals();
    });
}

void
CHANsignal(int sig, void (*handler)(int))
{
    bool first = !signals;

    if (first)
        signals.emplace(ioc());
    if (sig <= 0 || sig >= NSIG)
        return;
    if (signal_handlers[sig] == nullptr) {
        boost::system::error_code ec;
        signals->add(sig, ec);
        if (ec)
            syswarn("%s cant catch signal %d: %s", LogName, sig,
                    ec.message().c_str());
    }
    signal_handlers[sig] = handler;
    if (first)
        CHANarmsignals();
}


/*
**  In a child process about to exec, put the signals innd catches back to
**  their default disposition, so that none reaches Asio's handler (whose
**  pipe the child shares with innd).
*/
void
CHANsignalforked(void)
{
    for (int sig = 1; sig < NSIG; sig++)
        if (signal_handlers[sig] != nullptr)
            signal(sig, SIG_DFL);
}

/* We want to initialize four elements of CHANnull but let all of the other
   elements be initialized according to the standard rules for a static
   initializer.  However, GCC warns about incomplete initializer lists (since
   it may be a mistake), so we don't initialize anything at all here and
   instead explicitly set the first three values in CHANsetup.  This is the
   only reason why this isn't const. */
static CHANNEL CHANnull;


/*
**  Returns true if socket activation is used and the channel given as
**  as argument is used for socket activation.  Returns false otherwise.
*/
bool
CHANsystemdsa(CHANNEL *cp)
{
    const char *s;
    int count;

    s = getenv(INN_ENV_LISTEN_FDS_COUNT);
    if (s == nullptr)
        return false;

    count = atoi(s);

    if (cp->fd >= SD_LISTEN_FDS_START && cp->fd < SD_LISTEN_FDS_START + count)
        return true;

    return false;
}


/*
**  Tear down our world.  Free all of the allocated channels and clear all
**  global state data.  This function can also be used to initialize the
**  channels structure to a known empty state.
*/
void
CHANshutdown(void)
{
    CHANNEL *cp;
    int i;

    for (size_t fd = 0; fd < channels.io.size(); fd++)
        if (channels.io[fd] && channels.io[fd]->stream)
            CHANdropfd(static_cast<int>(fd));
    for (auto &s : channels.io)
        if (s)
            *s = chan_io();
    channels.sleep_count = 0;
    channels.max_fd = -1;
    channels.max_sleep_fd = -1;

    if (channels.table != nullptr) {
        cp = channels.table;
        for (i = channels.table_size; --i >= 0; cp++) {
            if (CHANsystemdsa(cp))
                continue;
            if (cp->Type != CTfree)
                CHANclose(cp, CHANname(cp));
            if (cp->In.data)
                free(cp->In.data);
            if (cp->Out.data)
                free(cp->Out.data);
        }
    }
    free(channels.table);
    channels.table = nullptr;
    channels.table_size = 0;
    channels.io.clear();
    if (channels.prioritized_size > 0) {
        free(channels.prioritized);
        channels.prioritized_size = 0;
    }
}


/*
**  Initialize the global I/O channel state and prepare for creation of new
**  channels.  Takes the number of channels to pre-create, which currently
**  must be as large as the largest file descriptor that will be used for a
**  channel.
*/
void
CHANsetup(int count)
{
    CHANNEL *cp;
    int i;

    CHANshutdown();
    channels.table_size = count;
    channels.table = xmalloc(count * sizeof(CHANNEL));
    channels.io.assign(count, nullptr);

    /* Finish initializing CHANnull, since we can't do this entirely with a
       static initializer without having to list every element in the
       incredibly long channel struct and update it whenever the channel
       struct changes. */
    CHANnull.Type = CTfree;
    CHANnull.State = CSerror;
    CHANnull.fd = -1;
    CHANnull.NextLog = innconf->chaninacttime;

    /* Now, we can use CHANnull to initialize all of the other channels. */
    for (cp = channels.table; --count >= 0; cp++)
        *cp = CHANnull;

    /* Reserve three slots for prioritized channels (control and AF_INET and
       AF_INET6 remconn).  If we end up with more somehow, we'll resize in
       CHANcreate. */
    channels.prioritized_size = 3;
    channels.prioritized = xmalloc(3 * sizeof(CHANNEL *));
    for (i = 0; i < 3; i++)
        channels.prioritized[i] = nullptr;
}


/*
**  Create a channel from a file descriptor.  Takes the channel type, the
**  initial state, the reader callback, and the writer callback.
*/
CHANNEL *
CHANcreate(int fd, enum channel_type type, enum channel_state state,
           innd_callback_func reader, innd_callback_func write_done)
{
    CHANNEL *cp;
    int i, j, size;
    struct buffer in = {0, 0, 0, nullptr};
    struct buffer out = {0, 0, 0, nullptr};

    /* Last time to ensure the file descriptor is not too high.  Callers have
       normally already handled that case before and not called CHANcreate().
       Note that the channel table, created in CHANsetup(), is known to have
       enough room. */
    if (!isvalidfd(fd)) {
        /* Go on without returning NULL, in case a caller is not prepared to
           receive such a response. */
        syswarn("%s file descriptor %d too high in CHANcreate (see "
                "rlimitnofile in inn.conf)",
                LogName, fd);
    }

    /* Currently, we do no dynamic allocation, but instead assume that the
       channel table is sized large enough to hold all possible file
       descriptors.  The difficulty with dynamically resizing is that it may
       invalidate any pointers into the channels, which hold all sorts of
       random things, including article data.

       FIXME: Design a better data structure that can be resized easily. */
    cp = &channels.table[fd];

    /* Don't lose the existing buffers or article state when overwriting
       with CHANnull. */
    ArticleState *article = cp->Data.State;
    in = cp->In;
    buffer_resize(&in, START_BUFF_SIZE);
    in.used = 0;
    in.left = in.size;
    out = cp->Out;
    buffer_resize(&out, SMBUF);
    buffer_set(&out, "", 0);

    /* Set up the channel's info.  Note that we don't have to initialize
       anything that's already set properly to zero in CHANnull. */
    *cp = CHANnull;
    cp->av = nullptr;
    cp->fd = fd;
    cp->Type = type;
    cp->State = state;
    cp->Reader = reader;
    cp->WriteDone = write_done;
    cp->Started = Now.tv_sec;
    cp->Started_checkpoint = Now.tv_sec;
    cp->LastActive = Now.tv_sec;
    cp->In = in;
    cp->Out = out;
    cp->Data.State = article != nullptr ? article : new ArticleState;
    cp->Tracing = Tracing;
    HashClear(&cp->CurrentMessageIDHash);
    ARTprepare(cp);

    /* A descriptor closed without CHANclose (or CHANdropfd) may have left
       state behind under this number; start from nothing. */
    CHANdropfd(fd);
    CHANattach(fd);

    fdflag_close_exec(fd, true);

#ifndef _HPUX_SOURCE
    /* Stupid HPUX 11.00 has a broken listen/accept where setting the listen
       socket to nonblocking prevents you from successfully setting the
       socket returned by accept(2) back to blocking mode, no matter what,
       resulting in all kinds of funny behaviour, data loss, etc. etc. */
    if (!fdflag_nonblocking(fd, true) && errno != ENOTSOCK && errno != ENOTTY)
        syswarn("%s cant nonblock %d", LogName, fd);
#endif

    /* Note the control and remconn channels, for efficiency. */
    if (type == CTcontrol || type == CTremconn) {
        for (i = 0; i < channels.prioritized_size; i++)
            if (channels.prioritized[i] == nullptr)
                break;
        if (i >= channels.prioritized_size) {
            size = channels.prioritized_size + 1;
            channels.prioritized =
                xrealloc(channels.prioritized, size * sizeof(CHANNEL *));
            for (j = channels.prioritized_size; j < size; j++)
                channels.prioritized[j] = nullptr;
            channels.prioritized_size = size;
        }
        channels.prioritized[i] = cp;
    }

    /* Return a pointer to the new channel. */
    return cp;
}


/*
**  Start or stop tracing a channel.  If we're turning on tracing, dump a
**  bunch of information about that particular channel.
*/
void
CHANtracing(CHANNEL *cp, bool flag)
{
    char *name;
    char addr[INET6_ADDRSTRLEN] = "?";

    name = CHANname(cp);
    notice("%s trace %s", name, flag ? "on" : "off");
    cp->Tracing = flag;
    if (flag) {
        notice("%s trace badwrites %lu blockwrites %lu badreads %lu", name,
               cp->BadWrites, cp->BlockedWrites, cp->BadReads);
        network_sockaddr_sprint(addr, sizeof(addr),
                                (struct sockaddr *) &cp->Address);
        notice("%s trace address %s lastactive %lu nextlog %lu", name, addr,
               (unsigned long) cp->LastActive, (unsigned long) cp->NextLog);
        if (is_sleeping(cp->fd))
            notice("%s trace sleeping %lu %p", name,
                   (unsigned long) cp->Waketime, (void *) cp->Waker);
        if (wants_read(cp->fd))
            notice("%s trace reading %lu %s", name,
                   (unsigned long) cp->In.used,
                   MaxLength(cp->In.data, cp->In.data));
        if (wants_write(cp->fd))
            notice("%s trace writing %lu %s", name,
                   (unsigned long) cp->Out.left,
                   MaxLength(cp->Out.data, cp->Out.data));
    }
}


/*
**  Close an NNTP channel.  Called by CHANclose to do the special handling for
**  NNTP channels, which carry quite a bit more data than other channels.
**
**  FIXME: There really should be a type-specific channel closing callback
**  that can take care of this, and this will be mandatory when all the
**  NNTP-specific stuff is moved out of the core channel struct.
*/
static void
CHANclose_nntp(CHANNEL *cp, const char *name)
{
    WIPprecomfree(cp);
    NCclearwip(cp);
    if (cp->State == CScancel)
        notice("%s closed seconds %lu cancels %lu", name,
               (unsigned long) (Now.tv_sec - cp->Started), cp->Received);
    else {
        notice(
            "%s checkpoint seconds %lu accepted %lu refused %lu rejected %lu"
            " duplicate %lu accepted size %llu duplicate size %llu"
            " rejected size %llu",
            name, (unsigned long) (Now.tv_sec - cp->Started_checkpoint),
            cp->Received - cp->Received_checkpoint,
            cp->Refused - cp->Refused_checkpoint,
            cp->Rejected - cp->Rejected_checkpoint,
            cp->Duplicate - cp->Duplicate_checkpoint,
            cp->Size - cp->Size_checkpoint,
            cp->DuplicateSize - cp->DuplicateSize_checkpoint,
            cp->RejectSize - cp->RejectSize_checkpoint);
        notice("%s closed seconds %lu accepted %lu refused %lu rejected %lu"
               " duplicate %lu accepted size %llu duplicate size %llu"
               " rejected size %llu",
               name, (unsigned long) (Now.tv_sec - cp->Started), cp->Received,
               cp->Refused, cp->Rejected, cp->Duplicate, cp->Size,
               cp->DuplicateSize, cp->RejectSize);
    }

    /* If this was a peer who was connection-limited, take note that one of
       their connections was just closed and possibly wake up another one.

       FIXME: This is a butt-ugly way of handling this and a layering
       violation to boot.  This needs to happen in the NC code, possibly via a
       channel closing callback. */
    if (cp->MaxCnx > 0) {
        char *label, *tmplabel;
        int tfd;
        CHANNEL *tempchan;

        label = RClabelname(cp);
        if (label != nullptr) {
            for (tfd = 0; tfd <= channels.max_fd; tfd++) {
                if (tfd == cp->fd)
                    continue;
                tempchan = &channels.table[tfd];
                if (tempchan->fd < 0 || tempchan->Type != CTnntp)
                    continue;
                tmplabel = RClabelname(tempchan);
                if (tmplabel == nullptr)
                    continue;
                if (strcmp(label, tmplabel) == 0 && tempchan->ActiveCnx != 0) {
                    tempchan->ActiveCnx = cp->ActiveCnx;
                    RCHANadd(tempchan);
                    break;
                }
            }
        }
    }
}


/*
**  Close a channel.
*/
void
CHANclose(CHANNEL *cp, const char *name)
{
    int i;

    if (cp->Type == CTfree)
        warn("%s internal closing free channel %d", name, cp->fd);
    else {
        if (cp->Type == CTnntp)
            CHANclose_nntp(cp, name);
        else if (cp->Type == CTreject) {
            /* Use cp->Rejected for the response code. */
            notice("%s %lu", name, cp->Rejected);
        } else if (cp->Out.left)
            warn("%s closed lost %lu", name, (unsigned long) cp->Out.left);
        else
            notice("%s closed", name);
        WCHANremove(cp);
        RCHANremove(cp);
        SCHANremove(cp);
        CHANdropfd(cp->fd);
        if (cp->fd >= 0 && close(cp->fd) < 0)
            syswarn("%s cant close %s", LogName, name);
        for (i = 0; i < channels.prioritized_size; i++)
            if (channels.prioritized[i] == cp)
                channels.prioritized[i] = nullptr;
    }

    /* Mark it unused. */
    cp->Type = CTfree;
    cp->State = CSerror;
    cp->fd = -1;
    if (cp->Argument != nullptr)
        free(cp->Argument);
    cp->Argument = nullptr;
    cp->ActiveCnx = 0;
    delete cp->Data.State;
    cp->Data.State = nullptr;

    /* Free the buffers if they got big. */
    if (cp->In.size > BIG_BUFFER) {
        cp->In.size = 0;
        cp->In.used = 0;
        cp->In.left = 0;
        free(cp->In.data);
        cp->In.data = nullptr;
    }
    if (cp->Out.size > BIG_BUFFER) {
        cp->Out.size = 0;
        cp->Out.used = 0;
        cp->Out.left = 0;
        free(cp->Out.data);
        cp->Out.data = nullptr;
    }

    /* Always free the Sendid buffer. */
    if (cp->Sendid.size > 0) {
        cp->Sendid.size = 0;
        cp->Sendid.used = 0;
        cp->Sendid.left = 0;
        free(cp->Sendid.data);
        cp->Sendid.data = nullptr;
    }

    /* Free the space allocated for NNTP commands. */
    if (cp->av != nullptr) {
        free(cp->av[0]);
        free(cp->av);
        cp->av = nullptr;
    }
}


/*
**  Return a printable name for the channel.
**
**  FIXME: Another layering violation.  Each type of channel should register a
**  callback or a name rather than making the general code know how to name
**  channels.
*/
char *
CHANname(CHANNEL *cp)
{
    int i;
    SITE *sp;
    const char *site;
    const char *type;
    pid_t pid;

    switch (cp->Type) {
    default:
        snprintf(cp->Name, sizeof(cp->Name), "?%u(#%d@%ld)?", cp->Type, cp->fd,
                 (long) (cp - channels.table));
        break;
    case CTany:
        snprintf(cp->Name, sizeof(cp->Name), "any:%d", cp->fd);
        break;
    case CTfree:
        snprintf(cp->Name, sizeof(cp->Name), "free:%d", cp->fd);
        break;
    case CTremconn:
        snprintf(cp->Name, sizeof(cp->Name), "remconn:%d", cp->fd);
        break;
    case CTreject:
        snprintf(cp->Name, sizeof(cp->Name), "%s rejected",
                 cp->Address.ss_family == 0 ? "localhost" : RChostname(cp));
        break;
    case CTnntp:
        snprintf(cp->Name, sizeof(cp->Name), "%s:%d",
                 cp->Address.ss_family == 0 ? "localhost" : RChostname(cp),
                 cp->fd);
        break;
    case CTlocalconn:
        snprintf(cp->Name, sizeof(cp->Name), "localconn:%d", cp->fd);
        break;
    case CTcontrol:
        snprintf(cp->Name, sizeof(cp->Name), "control:%d", cp->fd);
        break;
    case CTexploder:
    case CTfile:
    case CTprocess:
        /* Find the site that has this channel. */
        site = "?";
        pid = 0;
        for (i = nSites, sp = Sites.data(); --i >= 0; sp++)
            if (sp->Channel == cp) {
                site = sp->Name;
                if (cp->Type != CTfile)
                    pid = sp->pid;
                break;
            }
        if (cp->Type == CTfile)
            type = "file";
        else if (cp->Type == CTexploder)
            type = "exploder";
        else
            type = "proc";
        if (pid == 0)
            snprintf(cp->Name, sizeof(cp->Name), "%s:%d:%s",
                     MaxLength(site, site), cp->fd, type);
        else
            snprintf(cp->Name, sizeof(cp->Name), "%s:%d:%s:%ld",
                     MaxLength(site, site), cp->fd, type, (long) pid);
        break;
    }
    return cp->Name;
}


/*
**  Return the channel for a specified descriptor.
*/
CHANNEL *
CHANfromdescriptor(int fd)
{
    if (fd < 0 || fd >= channels.table_size)
        return nullptr;
    return &channels.table[fd];
}


/*
**  Iterate over all channels of a specified type.  The next channel is
**  returned and its index is put into ip, which serves as the cursor.
**
**  FIXME: Make ip an opaque cursor.
*/
CHANNEL *
CHANiter(int *ip, enum channel_type type)
{
    CHANNEL *cp;
    int i;

    for (i = *ip; i >= 0 && i < channels.table_size; i++) {
        cp = &channels.table[i];
        if (cp->Type == CTfree && cp->fd == -1)
            continue;
        if (type == CTany || cp->Type == type) {
            *ip = ++i;
            return cp;
        }
    }
    return nullptr;
}


/*
**  When removing a channel from the read and write masks, we want to lower
**  the last file descriptor if we removed the highest one.  Called from both
**  RCHANremove and WCHANremove.
*/
static void
CHANresetlast(int fd)
{
    if (fd == channels.max_fd)
        while (!wants_read(channels.max_fd)
               && !wants_write(channels.max_fd)
               && channels.max_fd > 1)
            channels.max_fd--;
}


/*
**  When removing a channel from the sleep mask, we want to lower the last
**  file descriptor if we removed the highest one.  Called from SCHANremove.
*/
static void
CHANresetlastsleeping(int fd)
{
    if (fd == channels.max_sleep_fd) {
        while (!is_sleeping(channels.max_sleep_fd)
               && channels.max_sleep_fd > 1)
            channels.max_sleep_fd--;
    }
}


/*
**  Mark a channel as an active reader.
*/
void
RCHANadd(CHANNEL *cp)
{
    chan_io *s = chan_state(cp->fd);
    if (s == nullptr)
        return;
    s->want_read = true;
    CHANarm(cp->fd);
    if (cp->fd > channels.max_fd)
        channels.max_fd = cp->fd;

    /* For non-NNTP channels, start reading at the beginning of the buffer. */
    if (cp->Type != CTnntp)
        cp->In.used = 0;
}


/*
**  Remove a channel from the set of readers.
*/
void
RCHANremove(CHANNEL *cp)
{
    if (wants_read(cp->fd)) {
        chan_io *s = chan_state(cp->fd);
        s->want_read = false;
        s->read_ready = false;
        CHANresetlast(cp->fd);
    }
}


/*
**  Put a channel to sleep and call a function when it wakes.  Note that arg
**  must be NULL or allocated memory, since it will be freed later.
*/
void
SCHANadd(CHANNEL *cp, time_t wake, void *event, innd_callback_func waker,
         void *arg)
{
    if (!CHANsleeping(cp)) {
        chan_io *s = chan_state(cp->fd);
        if (s == nullptr)
            return;
        channels.sleep_count++;
        s->sleeping = true;
    }
    if (cp->fd > channels.max_sleep_fd)
        channels.max_sleep_fd = cp->fd;
    cp->Waketime = wake;
    cp->Waker = waker;
    if (cp->Argument != arg) {
        free(cp->Argument);
        cp->Argument = arg;
    }
    cp->Event = event;
}


/*
**  Take a channel off the sleep list.
*/
void
SCHANremove(CHANNEL *cp)
{
    if (!CHANsleeping(cp))
        return;
    chan_state(cp->fd)->sleeping = false;
    channels.sleep_count--;
    cp->Waketime = 0;

    /* If this was the highest descriptor, get a new highest. */
    CHANresetlastsleeping(cp->fd);
}


/*
**  Is a channel on the sleep list?
*/
bool
CHANsleeping(CHANNEL *cp)
{
    return is_sleeping(cp->fd);
}


/*
**  Wake up channels waiting for a specific event.  (Or rather, mark them
**  ready to wake up; they'll actually be woken up the next time through the
**  main loop.)
*/
void
SCHANwakeup(void *event)
{
    CHANNEL *cp;
    int i;

    for (cp = channels.table, i = channels.table_size; --i >= 0; cp++)
        if (cp->Type != CTfree && cp->Event == event && CHANsleeping(cp))
            cp->Waketime = 0;
}


/*
**  Mark a channel as an active writer.  Don't reset the Out->left field
**  since we could have buffered I/O already in there.
*/
void
WCHANadd(CHANNEL *cp)
{
    if (cp->Out.left > 0) {
        chan_io *s = chan_state(cp->fd);
        if (s == nullptr)
            return;
        s->want_write = true;
        CHANarm(cp->fd);
        if (cp->fd > channels.max_fd)
            channels.max_fd = cp->fd;
    }
}


/*
**  Remove a channel from the set of writers.
*/
void
WCHANremove(CHANNEL *cp)
{
    if (wants_write(cp->fd)) {
        chan_io *s = chan_state(cp->fd);
        s->want_write = false;
        s->write_ready = false;
        CHANresetlast(cp->fd);

        /* No data left -- reset used so we don't grow the buffer. */
        if (cp->Out.left <= 0) {
            cp->Out.used = 0;
            cp->Out.left = 0;
        }
    }
}


/*
**  Set a channel to start off with the contents of an existing channel.
*/
void
WCHANsetfrombuffer(CHANNEL *cp, struct buffer *bp)
{
    WCHANset(cp, &bp->data[bp->used], bp->left);
}


/*
**  Internal function to resize the in buffer for a given channel to the new
**  size given, adjusting pointers as required.
*/
static void
CHANresize(CHANNEL *cp, size_t size)
{
    struct buffer *bp;
    char *p;
    size_t change;
    ptrdiff_t offset;
    int i;
    HDRCONTENT *hc = cp->Data.HdrContent;

    bp = &cp->In;
    change = size - bp->size;
    bp->size = size;
    bp->left += change;
    p = bp->data;

    /* Reallocate the buffer and adjust offsets if realloc moved the location
       of the memory region.  Only adjust offsets if we're in a state where we
       care about the header contents.

       FIXME: This is invalid C, although it will work on most (all?)  common
       systems.  The pointers need to be reduced to offsets and then turned
       back into relative pointers rather than adjusting the pointers
       directly, since as soon as realloc is called, pointers into the old
       space become invalid and may not be used further, even for arithmetic.
       (Not to mention that two pointers to different objects may not be
       compared and arithmetic may not be performed on them. */
    TMRstart(TMR_DATAMOVE);
    bp->data = xrealloc(bp->data, bp->size);
    offset = p - bp->data;
    if (offset != 0) {
        if (cp->State == CSgetheader || cp->State == CSgetbody
            || cp->State == CSeatarticle) {
            if (cp->Data.BytesHeader != nullptr)
                cp->Data.BytesHeader -= offset;
            for (i = 0; i < MAX_ARTHEADER; i++, hc++)
                hc->rebase(offset);
        }
    }
    TMRstop(TMR_DATAMOVE);
}


/*
**  Read in text data, return the amount we read.
*/
int
CHANreadtext(CHANNEL *cp)
{
    struct buffer *bp;
    char *name;
    int oerrno, maxbyte;
    ssize_t count;

    /* Grow buffer if we're getting close to current limit.

       FIXME: The In buffer doesn't use the normal meanings of .used and
       .left. */
    bp = &cp->In;
    bp->left = bp->size - bp->used;
    if (bp->left <= LOW_WATER)
        CHANresize(cp, bp->size + GROW_AMOUNT(bp->size));

    /* Read in whatever is there, up to some reasonable limit.

       We want to limit the amount of time devoted to processing the incoming
       data for any given channel.  There's no easy way of doing that, though,
       so we restrict the data size instead.

       If the data is part of a single large article, then reading and
       processing many kilobytes at a time costs very little.  If the data is
       a long list of CHECK commands from a streaming feed, then every line of
       data will require a history lookup, and we probably don't want to do
       more than about 10 of those per channel on each cycle of the main
       select() loop (otherwise we might take too long before giving other
       channels a turn).  10 lines of CHECK commands suggests a limit of about
       1KB of data, or less, which is the innconf->maxcmdreadsize default.  If
       innconf->maxcmdreadsize is 0, there is no limit.

       Reduce the read size only if we're reading commands.

       FIXME: A better approach would be to limit the number of commands we
       process for each channel. */
    if (innconf->maxcmdreadsize == 0 || cp->State != CSgetcmd
        || bp->left < innconf->maxcmdreadsize)
        maxbyte = bp->left;
    else
        maxbyte = innconf->maxcmdreadsize;
    TMRstart(TMR_NNTPREAD);
    count = read(cp->fd, &bp->data[bp->used], maxbyte);
    TMRstop(TMR_NNTPREAD);

    /* Solaris (at least 2.4 through 2.6) will occasionally return EAGAIN in
       response to a read even if the file descriptor already selected true
       for reading, apparently due to some internal resource exhaustion.  In
       that case, return -2, which will drop back out to the main loop and go
       on to the next file descriptor, as if the descriptor never selected
       true.  This check will probably never trigger on platforms other than
       Solaris. */
    if (count < 0) {
        if (errno == EAGAIN)
            return -2;
        oerrno = errno;
        name = CHANname(cp);
        errno = oerrno;
        sysnotice("%s cant read", name);
        return -1;
    }
    if (count == 0) {
        name = CHANname(cp);
        notice("%s readclose", name);
        return 0;
    }
    bp->used += count;
    bp->left -= count;
    return count;
}


/*
**  Internal function to write out channel data.
**
**  If I/O backs up a lot, we can get EMSGSIZE on some systems.  If that
**  happens we want to do the I/O in chunks.  We assume stdio's BUFSIZ is a
**  good chunk value.
**
**  FIXME: Roll this code into xwrite.
*/
static int
CHANwrite(int fd, const char *data, long length)
{
    ssize_t count;
    const char *p;

    /* Try the standard case -- write it all. */
    do {
        count = write(fd, data, length);
    } while (count < 0 && errno == EINTR);
    if (count > 0 || (count < 0 && errno != EMSGSIZE))
        return count;

    /* We got EMSGSIZE, so write it in pieces. */
    for (p = data, count = 0; length > 0; p += count, length -= count) {
        count = write(fd, p, (length > BUFSIZ ? BUFSIZ : length));
        if (count < 0 && errno == EINTR) {
            count = 0;
            continue;
        }
        if (count <= 0)
            break;
    }

    /* Return error, or partial results if we got something. */
    return (p == data) ? count : (p - data);
}


/*
**  Try to flush out the buffer.  Use this only on file channels!
*/
bool
WCHANflush(CHANNEL *cp)
{
    struct buffer *bp;
    ssize_t count;

    /* Write it. */
    for (bp = &cp->Out; bp->left > 0; bp->left -= count, bp->used += count) {
        count = CHANwrite(cp->fd, &bp->data[bp->used], bp->left);
        if (count <= 0) {
            syswarn("%s cant flush count %lu", CHANname(cp),
                    (unsigned long) bp->left);
            return false;
        }
        if (count == 0) {
            warn("%s cant flush count %lu", CHANname(cp),
                 (unsigned long) bp->left);
            return false;
        }
    }
    WCHANremove(cp);
    return true;
}


/*
**  Standard wakeup routine called after a write channel was put to sleep.
*/
static void
CHANwakeup(CHANNEL *cp)
{
    notice("%s wakeup", CHANname(cp));
    WCHANadd(cp);
}


/*
**  Attempting to write would block; stop output or give up.
*/
static void
CHANwritesleep(CHANNEL *cp, const char *name)
{
    unsigned long bad, delay;

    bad = ++(cp->BlockedWrites);
    if (bad > innconf->badiocount)
        switch (cp->Type) {
        default:
            break;
        case CTreject:
        case CTnntp:
        case CTfile:
        case CTexploder:
        case CTprocess:
            warn("%s blocked closing", name);
            SITEchanclose(cp);
            CHANclose(cp, name);
            return;
        }
    delay = bad * innconf->blockbackoff;
    warn("%s blocked sleeping %lu", name, delay);
    SCHANadd(cp, Now.tv_sec + delay, nullptr, CHANwakeup, nullptr);
}


/*
**  Count the number of active connections for a given peer.
**
**  FIXME: This seems like an overly cumbersome way to do this, and also seems
**  like a layering violation.  Do we really have to do this here?
*/
void
CHANcount_active(CHANNEL *cp)
{
    int found;
    CHANNEL *tempchan;
    char *label, *tmplabel;
    int fd;

    if (cp->fd < 0 || cp->Type != CTnntp || cp->ActiveCnx == 0)
        return;
    found = 1;
    label = RClabelname(cp);
    if (label == nullptr)
        return;
    for (fd = 0; fd <= channels.max_fd; fd++) {
        tempchan = &channels.table[fd];
        tmplabel = RClabelname(tempchan);
        if (tmplabel == nullptr)
            continue;
        if (strcmp(label, tmplabel) == 0 && tempchan->ActiveCnx != 0)
            found++;
    }
    cp->ActiveCnx = found;
}


/*
**  Handle a file descriptor that selects ready to read.  Dispatch to the
**  reader function, and then resize its input buffer if needed.
*/
static void
CHANhandle_read(CHANNEL *cp)
{
    size_t size;

    if (cp->Type == CTfree) {
        warn("%s %d free but was in RMASK", CHANname(cp), cp->fd);
        RCHANremove(cp);
        CHANdropfd(cp->fd);
        close(cp->fd);
        cp->fd = -1;
        return;
    }
    cp->LastActive = Now.tv_sec;
    (*cp->Reader)(cp);

    /* Check and see if the buffer is grossly overallocated and shrink if
       needed. */
    if (cp->In.size <= BIG_BUFFER)
        return;
    if (cp->In.used == 0)
        CHANresize(cp, START_BUFF_SIZE);
    else if ((cp->In.size / cp->In.used) > 10) {
        size = cp->In.used * 2;
        if (size < START_BUFF_SIZE)
            size = START_BUFF_SIZE;
        CHANresize(cp, size);
    }
}


/*
**  Handle a file descriptor that selects ready to write.  Write out the
**  pending data.
*/
static void
CHANhandle_write(CHANNEL *cp)
{
    struct buffer *bp;
    ssize_t count;
    int oerrno;
    const char *name;

    if (cp->Type == CTfree) {
        warn("%s %d free but was in WMASK", CHANname(cp), cp->fd);
        WCHANremove(cp);
        CHANdropfd(cp->fd);
        close(cp->fd);
        cp->fd = -1;
        return;
    }
    bp = &cp->Out;
    if (bp->left == 0) {
        /* Should not be possible. */
        WCHANremove(cp);
        return;
    }
    cp->LastActive = Now.tv_sec;
    count = CHANwrite(cp->fd, &bp->data[bp->used], bp->left);
    if (count <= 0) {
        oerrno = errno;
        name = CHANname(cp);
        errno = oerrno;
        if (oerrno == EWOULDBLOCK)
            oerrno = EAGAIN;
        if (count < 0)
            sysnotice("%s cant write", name);
        else
            notice("%s cant write", name);
        cp->BadWrites++;
        if (count < 0 && oerrno == EPIPE) {
            SITEchanclose(cp);
            CHANclose(cp, name);
        } else if (count < 0 && oerrno == EAGAIN) {
            WCHANremove(cp);
            CHANwritesleep(cp, name);
        } else if (cp->BadWrites >= innconf->badiocount) {
            warn("%s sleeping", name);
            WCHANremove(cp);
            SCHANadd(cp, Now.tv_sec + innconf->pauseretrytime, nullptr,
                     CHANwakeup, nullptr);
        }
    } else {
        cp->BadWrites = 0;
        cp->BlockedWrites = 0;
        bp->left -= count;
        bp->used += count;
        if (bp->left > 0)
            buffer_compact(bp);
        else {
            WCHANremove(cp);
            (*cp->WriteDone)(cp);
        }
    }
}


/*
**  Main I/O loop.  Wait for data, call the channel's handler when there is
**  something to read or when the queued write is finished.  In order to be
**  fair (i.e., don't always give descriptor n priority over n+1), we remember
**  where we last had something and pick up from there.
**
**  Yes, the main code has really wandered over to the side a lot.
*/
void
CHANreadloop(void)
{
    int i, startpoint, count, lastfd;
    CHANNEL *cp;
    unsigned long silence;
    const char *name;
    time_t last_sync;
    int fd = 0;

    STATUSinit();
    gettimeofday(&Now, nullptr);
    last_sync = Now.tv_sec;
    ioc();

    while (true) {
        /* See if any processes died. */
        PROCscan();

        /* How long to wait: the main loop timeout, cut short for the timer
           summary and for the earliest sleeping channel's wake time. */
        long wait = TimeOut.tv_sec;
        if (innconf->timer != 0) {
            unsigned long now = TMRnow();

            if (now < 1000 * innconf->timer)
                wait = innconf->timer - now / 1000;
            else {
                TMRsummary("ME", timer_name);
                InndHisLogStats();
                wait = innconf->timer;
            }
        }
        if (channels.sleep_count > 0) {
            int last = channels.max_sleep_fd;

            for (i = 0; i <= last; i++) {
                if (!is_sleeping(i))
                    continue;
                long until = channels.table[i].Waketime - Now.tv_sec;
                if (until < wait)
                    wait = until < 0 ? 0 : until;
            }
        }

        /* Arm a wait for every channel that wants one; descriptors that
           cannot be waited on are ready at once. */
        bool ready = false;
        for (i = 0; i <= channels.max_fd; i++) {
            if (!channels.io[i])
                continue;
            CHANarm(i);
            if (channels.io[i]->read_ready || channels.io[i]->write_ready)
                ready = true;
        }

        /* Wait for readiness, the timeout or a signal, then collect
           everything else that is already ready. */
        TMRstart(TMR_IDLE);
        if (!ready && wait > 0) {
            bool expired = false;

            wait_timer->expires_after(std::chrono::seconds(wait));
            wait_timer->async_wait([&expired](const boost::system::error_code &ec) {
                if (!ec)
                    expired = true;
            });
            io->run_one();
            if (!expired)
                wait_timer->cancel();
        }
        io->poll();
        if (io->stopped())
            io->restart();
        TMRstop(TMR_IDLE);

        count = 0;
        for (i = 0; i <= channels.max_fd; i++) {
            if (!channels.io[i])
                continue;
            if (channels.io[i]->read_ready)
                count++;
            if (channels.io[i]->write_ready)
                count++;
        }

        STATUSmainloophook();
        if (GotTerminate) {
#ifdef DO_PERL
            PLmode(Mode, OMshutdown, (char *) "exiting due to signal");
#endif
#ifdef DO_PYTHON
            PYmode(Mode, OMshutdown, (char *) "exiting due to signal");
#endif
            notice("%s exiting due to signal", LogName);
            CleanupAndExit(0, nullptr);
        }

        /* Update the "reasonably accurate" time. */
        gettimeofday(&Now, nullptr);
        if (Now.tv_sec > last_sync + TimeOut.tv_sec) {
            HISsync(History);
            if (ICDactivedirty != 0) {
                ICDwriteactive();
                ICDactivedirty = 0;
            }
            last_sync = Now.tv_sec;
        }

        /* If no channels are active, flush and skip if nobody's sleeping. */
        if (count == 0) {
            if (Mode == OMrunning)
                ICDwrite();
            if (channels.sleep_count == 0)
                continue;
        }

        /* Try the prioritized channels first. */
        for (i = 0; i < channels.prioritized_size; i++) {
            int pfd;

            if (channels.prioritized[i] == nullptr)
                continue;
            pfd = channels.prioritized[i]->fd;
            if (wants_read(pfd) && channels.io[pfd]->read_ready) {
                channels.io[pfd]->read_ready = false;
                count--;
                if (count > 4)
                    count = 4; /* might be more requests */
                (*channels.prioritized[i]->Reader)(channels.prioritized[i]);
            }
        }

        /* Loop through all active channels.  Somebody could have closed a
           channel so we double-check the global mask before looking at what
           select returned.  The code here is written so that a channel could
           be reading and writing and sleeping at the same time, even though
           that's not possible.  (Just as well, since in SysVr4 the count
           would be wrong.) */
        lastfd = channels.max_fd;
        if (lastfd < channels.max_sleep_fd)
            lastfd = channels.max_sleep_fd;
        if (fd > lastfd)
            fd = 0;
        startpoint = fd;
        do {
            cp = &channels.table[fd];

            /* Check to see if this peer has too many open connections, and if
               so, either close or make inactive this connection. */
            if (cp->Type == CTnntp && cp->MaxCnx > 0 && cp->HoldTime > 0) {
                CHANcount_active(cp);
                if (cp->ActiveCnx > cp->MaxCnx && cp->fd > 0) {
                    if (cp->Started + cp->HoldTime < Now.tv_sec)
                        CHANclose(cp, CHANname(cp));
                    else {
                        if (fd >= lastfd)
                            fd = 0;
                        else
                            fd++;
                        cp->ActiveCnx = 0;
                        RCHANremove(cp);
                    }
                    continue;
                }
            }

            /* Anything to read? */
            if (wants_read(fd) && channels.io[fd]->read_ready) {
                channels.io[fd]->read_ready = false;
                count--;
                CHANhandle_read(cp);
            }

            /* Possibly recheck for dead children so we don't get SIGPIPE on
               readerless channels. */
            if (PROCneedscan)
                PROCscan();

            /* Ready to write? */
            if (wants_write(fd) && channels.io[fd]->write_ready) {
                channels.io[fd]->write_ready = false;
                count--;
                CHANhandle_write(cp);
            }

            /* Coming off a sleep? */
            if (is_sleeping(fd)
                && cp->Waketime <= Now.tv_sec) {
                if (cp->Type == CTfree) {
                    warn("%s %d free but was in SMASK", CHANname(cp), fd);
                    chan_state(fd)->sleeping = false;
                    channels.sleep_count--;
                    CHANresetlastsleeping(fd);
                    CHANdropfd(fd);
                    close(fd);
                    cp->fd = -1;
                } else {
                    cp->LastActive = Now.tv_sec;
                    SCHANremove(cp);
                    if (cp->Waker != nullptr) {
                        (*cp->Waker)(cp);
                    } else {
                        name = CHANname(cp);
                        warn("%s %d sleeping without Waker", name, fd);
                        SITEchanclose(cp);
                        CHANclose(cp, name);
                    }
                }
            }

            /* Toss CTreject channel early if it's inactive. */
            if (cp->Type == CTreject
                && cp->LastActive + REJECT_TIMEOUT < Now.tv_sec) {
                name = CHANname(cp);
                notice("%s timeout reject", name);
                CHANclose(cp, name);
            }

            /* Has this channel been inactive very long? */
            if (cp->Type == CTnntp
                && cp->LastActive + cp->NextLog < Now.tv_sec) {
                name = CHANname(cp);
                silence = Now.tv_sec - cp->LastActive;
                cp->NextLog += innconf->chaninacttime;
                notice("%s inactive %lu", name, silence / 60L);
                if (silence > innconf->peertimeout) {
                    notice("%s timeout", name);
                    CHANclose(cp, name);
                }
            }

            /* Bump pointer, modulo the table size. */
            if (fd >= lastfd)
                fd = 0;
            else
                fd++;

            /* If there is nothing to do, break out.

               FIXME: Do we really want to keep going until sleep_count is
               zero?  That means that when there are sleeping channels, we do
               a full traversal every time through the select loop. */
            if (count == 0 && channels.sleep_count == 0)
                break;
        } while (fd != startpoint);
    }
}
