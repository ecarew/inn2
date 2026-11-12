/*
**  Boost.Asio helpers shared by INN's event-loop programs (ASIO_PLAN.md).
**
**  C++ only.  A program that uses these runs one boost::asio::io_context
**  on one thread; the handlers below are called from that io_context.
*/

#ifndef INN_ASIO_H
#define INN_ASIO_H 1

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/system/error_code.hpp>

#include "inn/qio.h"

namespace inn {

/*
**  Reads newline-terminated lines from a descriptor, by QIO's rules:
**
**  - Each line is passed to the line handler without its newline.
**  - A line of max_line bytes or more, not counting its newline, is not
**    passed on; the long-line handler is called once for it instead, and
**    reading goes on with the next line.
**  - A partial line at end of file is dropped (the long-line handler is
**    called if it was already too long).
**  - At end of file the end handler gets boost::asio::error::eof; on a
**    read error it gets that error.  Nothing is called after it.
**
**  A pipe or a socket is read asynchronously.  Some descriptors cannot be
**  waited on: epoll refuses regular files, and macOS's kqueue never reports
**  end of file on a named FIFO or a terminal.  Those are read by a thread
**  with blocking reads, which hands each chunk to the io_context and waits
**  until it is handled, so the program's timers and signals keep running
**  and a large file is never held in memory.
**
**  The descriptor is not closed; the reader reads a dup() of it.  If the
**  reader is destroyed while its thread is blocked in read(), the thread
**  is detached and stops after that read; so destroy the reader only after
**  its end handler has run, or at process exit.
*/
class LineReader {
public:
    using LineHandler = std::function<void(const std::string &line)>;
    using LongHandler = std::function<void()>;
    using EndHandler = std::function<void(const boost::system::error_code &)>;

    LineReader(boost::asio::io_context &io, int fd,
               std::size_t max_line = QIO_BUFFERSIZE);
    ~LineReader();
    LineReader(const LineReader &) = delete;
    LineReader &operator=(const LineReader &) = delete;

    /* Start reading.  Call once. */
    void start(LineHandler on_line, LongHandler on_long, EndHandler on_end);

    /* Whether the descriptor is read asynchronously (not by a thread). */
    bool asynchronous() const { return async_; }

private:
    struct Shared;

    void next();
    bool got(const boost::system::error_code &ec, std::size_t n);
    void collect(const char *text, std::size_t len);

    boost::asio::io_context &io_;
    int fd_;
    std::size_t max_line_;
    boost::asio::posix::stream_descriptor in_;
    bool async_ = false;
    bool long_ = false;
    bool ended_ = false;
    std::string pending_;
    std::shared_ptr<Shared> shared_;
    /* Keeps io_context::run() from returning while the thread reads. */
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    LineHandler on_line_;
    LongHandler on_long_;
    EndHandler on_end_;
};

/*
**  The signals that end a program.  The first one received calls the
**  handler with its number; the set is then cleared, which gives every
**  signal in it back its default action, so a second one kills the
**  program at once.
*/
class ExitSignals {
public:
    ExitSignals(boost::asio::io_context &io, std::initializer_list<int> sigs,
                std::function<void(int signo)> on_first);

private:
    boost::asio::signal_set set_;
    std::function<void(int)> on_first_;
};

} // namespace inn

#endif /* !INN_ASIO_H */
