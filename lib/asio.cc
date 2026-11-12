/*
**  Boost.Asio helpers shared by INN's event-loop programs.  See
**  include/inn/asio.h.
*/

#include "portable/system.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <semaphore>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#include "inn/asio.h"

namespace asio = boost::asio;

namespace inn {

namespace {

/* Whether an io_context can wait on a descriptor. */
bool
waitable(int fd)
{
    struct stat st;

    if (fstat(fd, &st) < 0 || S_ISREG(st.st_mode))
        return false;
#if defined(__APPLE__)
    /* kqueue misses end of file on these.  A pipe has no links; a named
       FIFO has one. */
    if ((S_ISFIFO(st.st_mode) && st.st_nlink > 0) || S_ISCHR(st.st_mode))
        return false;
#endif
    return true;
}

} // namespace


/*
**  What the reading thread shares with the reader: the chunk it reads into
**  and the handshake.  Owned jointly, so a reader destroyed while the
**  thread is blocked in read() leaves it nothing freed to write into.
*/
struct LineReader::Shared {
    explicit Shared(std::size_t size) : chunk(size) {}

    std::vector<char> chunk;
    std::binary_semaphore done{0}; /* the chunk was handled */
    std::atomic<bool> more{true};      /* and whether to read more */
    std::atomic<bool> orphaned{false}; /* the reader is gone */
    std::thread thread;
};


LineReader::LineReader(asio::io_context &io, int fd, std::size_t max_line)
    : io_(io), fd_(fd), max_line_(max_line), in_(io),
      shared_(std::make_shared<Shared>(QIO_BUFFERSIZE))
{
    if (waitable(fd)) {
        boost::system::error_code ec;
        const int copy = dup(fd);
        if (copy >= 0) {
            in_.assign(copy, ec);
            async_ = !ec;
            if (ec)
                close(copy);
        }
    }
}


LineReader::~LineReader()
{
    if (shared_->thread.joinable()) {
        if (ended_)
            shared_->thread.join();
        else {
            shared_->orphaned = true;
            shared_->more = false;
            shared_->done.release();
            shared_->thread.detach();
            work_.reset();
        }
    }
}


void
LineReader::start(LineHandler on_line, LongHandler on_long, EndHandler on_end)
{
    on_line_ = std::move(on_line);
    on_long_ = std::move(on_long);
    on_end_ = std::move(on_end);

    if (async_) {
        next();
        return;
    }

    work_.emplace(io_.get_executor());

    /* The thread's loop.  It holds only the shared state and the
       io_context; each chunk is handled by a posted handler, and the chunk
       is not touched again until that handler is done. */
    shared_->thread = std::thread([this, shared = shared_, fd = fd_] {
        while (true) {
            ssize_t n;
            do
                n = read(fd, shared->chunk.data(), shared->chunk.size());
            while (n < 0 && errno == EINTR);
            if (shared->orphaned)
                return;
            boost::system::error_code ec;
            if (n < 0)
                ec.assign(errno, boost::system::system_category());
            else if (n == 0)
                ec = asio::error::eof;
            asio::post(io_, [this, shared, ec, n] {
                if (!shared->orphaned)
                    shared->more =
                        got(ec, ec ? 0 : static_cast<std::size_t>(n));
                shared->done.release();
            });
            shared->done.acquire();
            if (!shared->more || shared->orphaned)
                return;
        }
    });
}


void
LineReader::next()
{
    in_.async_read_some(asio::buffer(shared_->chunk),
                        [this](const boost::system::error_code &ec,
                               std::size_t n) {
                            if (got(ec, n))
                                next();
                        });
}


/* Add text to the line being collected, unless it is already too long. */
void
LineReader::collect(const char *text, std::size_t len)
{
    if (long_)
        return;
    pending_.append(text, len);
    if (pending_.size() >= max_line_) {
        long_ = true;
        pending_.clear();
    }
}


/* Handle a chunk of input, or the end of it.  Returns whether to read
   more. */
bool
LineReader::got(const boost::system::error_code &ec, std::size_t n)
{
    if (ec) {
        if (ec == asio::error::eof && long_)
            on_long_();
        ended_ = true;
        work_.reset();
        on_end_(ec);
        return false;
    }
    const char *p = shared_->chunk.data();
    const char *end = p + n;
    while (p < end) {
        const char *nl = static_cast<const char *>(memchr(p, '\n', end - p));
        if (nl == nullptr) {
            collect(p, end - p);
            break;
        }
        collect(p, nl - p);
        if (long_)
            on_long_();
        else
            on_line_(pending_);
        long_ = false;
        pending_.clear();
        p = nl + 1;
    }
    return true;
}


ExitSignals::ExitSignals(asio::io_context &io, std::initializer_list<int> sigs,
                         std::function<void(int)> on_first)
    : set_(io), on_first_(std::move(on_first))
{
    for (int s : sigs)
        set_.add(s);
    set_.async_wait([this](const boost::system::error_code &ec, int signo) {
        if (ec)
            return;
        set_.clear();
        on_first_(signo);
    });
}

} // namespace inn
