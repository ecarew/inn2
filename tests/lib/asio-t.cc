/* Test suite for lib/asio.cc: inn::LineReader and inn::ExitSignals. */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "inn/asio.h"
#include "inn/libinn.h"
#include "tap/basic.h"

namespace asio = boost::asio;

namespace {

/* What a reader saw. */
struct Seen {
    std::vector<std::string> lines;
    int longs = 0;
    bool ended = false;
    boost::system::error_code end;
    std::vector<std::string> events; /* "line", "long", "end", "timer" */
};

/* Read fd to its end with a LineReader, recording what it reports. */
Seen
readAll(int fd, std::size_t max_line = QIO_BUFFERSIZE,
        bool *asynchronous = nullptr)
{
    asio::io_context io;
    Seen seen;
    inn::LineReader reader(io, fd, max_line);
    if (asynchronous != nullptr)
        *asynchronous = reader.asynchronous();
    reader.start(
        [&](const std::string &line) {
            seen.lines.push_back(line);
            seen.events.push_back("line");
        },
        [&] {
            seen.longs++;
            seen.events.push_back("long");
        },
        [&](const boost::system::error_code &ec) {
            seen.ended = true;
            seen.end = ec;
            seen.events.push_back("end");
        });
    io.run();
    return seen;
}

/* A pipe whose write end a child fills with data and then closes; returns
   the read end.  A child, because data may not fit in the pipe buffer. */
int
pipeOf(const std::string &data, pid_t *child)
{
    int fds[2];
    if (pipe(fds) < 0)
        sysbail("cannot create pipe");
    *child = fork();
    if (*child < 0)
        sysbail("cannot fork");
    if (*child == 0) {
        close(fds[0]);
        if (xwrite(fds[1], data.data(), data.size()) < 0)
            _exit(1);
        _exit(0);
    }
    close(fds[1]);
    return fds[0];
}

/* A regular file holding data, opened for reading. */
int
fileOf(const std::string &data)
{
    const char *name = "asio-t.tmp";
    int fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        sysbail("cannot create %s", name);
    if (xwrite(fd, data.data(), data.size()) < 0)
        sysbail("cannot write %s", name);
    lseek(fd, 0, SEEK_SET);
    unlink(name);
    return fd;
}

const std::string sample = "one\n\ntwo words\nthree\npartial";
const std::vector<std::string> sampleLines = {"one", "", "two words",
                                              "three"};

/* The checks that apply to the sample on any kind of descriptor. */
void
checkSample(const Seen &seen, const char *kind)
{
    ok(seen.lines == sampleLines, "%s: lines in order, without newlines",
       kind);
    is_int(0, seen.longs, "%s: no long lines", kind);
    ok(seen.ended && seen.end == asio::error::eof,
       "%s: end handler gets eof; the partial last line is dropped", kind);
}

} // namespace


int
main(void)
{
    pid_t child;
    int fd, status;
    bool async;

    plan(23);

    /* A pipe: read asynchronously. */
    fd = pipeOf(sample, &child);
    Seen seen = readAll(fd, QIO_BUFFERSIZE, &async);
    waitpid(child, &status, 0);
    ok(async, "pipe: read asynchronously");
    checkSample(seen, "pipe");
    close(fd);

    /* A regular file: read by the thread. */
    fd = fileOf(sample);
    seen = readAll(fd, QIO_BUFFERSIZE, &async);
    ok(!async, "file: read by a thread");
    checkSample(seen, "file");
    close(fd);

    /* Long lines, at the limit and over it, through a pipe larger than
       any pipe buffer. */
    const std::string limit(9, 'x'); /* max_line 10: 9 is the longest */
    std::string data = "a\n" + limit + "\n" + std::string(10, 'y') + "\nb\n"
                       + std::string(100000, 'z') + "\nc\n";
    fd = pipeOf(data, &child);
    seen = readAll(fd, 10);
    waitpid(child, &status, 0);
    ok(seen.lines == std::vector<std::string>({"a", limit, "b", "c"}),
       "long: a line under the limit is kept, others go on");
    is_int(2, seen.longs, "long: each long line reported once");
    ok(seen.events
           == std::vector<std::string>(
               {"line", "line", "long", "line", "long", "line", "end"}),
       "long: reported in its place among the lines");
    close(fd);

    /* The same through the thread. */
    fd = fileOf(data);
    seen = readAll(fd, 10);
    ok(seen.lines == std::vector<std::string>({"a", limit, "b", "c"})
           && seen.longs == 2,
       "long: the same from a regular file");
    close(fd);

    /* A long line cut off by end of file is still reported. */
    fd = fileOf("a\n" + std::string(50, 'q'));
    seen = readAll(fd, 10);
    ok(seen.events == std::vector<std::string>({"line", "long", "end"}),
       "long: a long partial last line is reported before the end");
    close(fd);

    /* Empty input. */
    fd = fileOf("");
    seen = readAll(fd);
    ok(seen.lines.empty() && seen.ended && seen.end == asio::error::eof,
       "empty input: just the end");
    close(fd);

    /* A read error goes to the end handler. */
    fd = open(".", O_RDONLY);
    if (fd < 0)
        sysbail("cannot open .");
    seen = readAll(fd);
    ok(seen.ended && seen.end && seen.end != asio::error::eof,
       "a read error goes to the end handler (%s)",
       seen.end.message().c_str());
    is_int(0, static_cast<long>(seen.lines.size()), "no lines from an error");
    close(fd);

    /* A named FIFO with a slow writer: timers run while the reader waits,
       whether it is read asynchronously (Linux) or by the thread (macOS). */
    const char *fifo = "asio-t.fifo";
    unlink(fifo);
    if (mkfifo(fifo, 0600) < 0)
        sysbail("cannot make %s", fifo);
    child = fork();
    if (child < 0)
        sysbail("cannot fork");
    if (child == 0) {
        int w = open(fifo, O_WRONLY);
        if (w < 0)
            _exit(1);
        usleep(300000);
        if (xwrite(w, "late\n", 5) < 0)
            _exit(1);
        _exit(0);
    }
    fd = open(fifo, O_RDONLY);
    if (fd < 0)
        sysbail("cannot open %s", fifo);
    {
        asio::io_context io;
        Seen fseen;
        inn::LineReader reader(io, fd);
        asio::steady_timer timer(io, std::chrono::milliseconds(100));
        timer.async_wait([&](const boost::system::error_code &) {
            fseen.events.push_back("timer");
        });
        reader.start(
            [&](const std::string &line) {
                fseen.lines.push_back(line);
                fseen.events.push_back("line");
            },
            [&] { fseen.longs++; },
            [&](const boost::system::error_code &ec) {
                fseen.end = ec;
                fseen.events.push_back("end");
            });
        io.run();
        ok(fseen.events
               == std::vector<std::string>({"timer", "line", "end"}),
           "fifo: a timer fires while the reader waits");
        ok(fseen.lines == std::vector<std::string>({"late"})
               && fseen.end == asio::error::eof,
           "fifo: the line, then end of file when the writer closes");
    }
    waitpid(child, &status, 0);
    close(fd);
    unlink(fifo);

    /* The reader does not close the caller's descriptor. */
    fd = fileOf("x\n");
    readAll(fd);
    ok(fcntl(fd, F_GETFD) >= 0, "the caller's descriptor stays open");
    close(fd);

    /* Destroying a reader whose thread is blocked does not hang. */
    {
        int fds[2];
        if (pipe(fds) < 0)
            sysbail("cannot create pipe");
        unlink(fifo);
        if (mkfifo(fifo, 0600) < 0)
            sysbail("cannot make %s", fifo);
        int r = open(fifo, O_RDONLY | O_NONBLOCK);
        int w = open(fifo, O_WRONLY);
        fcntl(r, F_SETFL, fcntl(r, F_GETFL) & ~O_NONBLOCK);
        {
            asio::io_context io;
            inn::LineReader reader(io, r);
            reader.start([](const std::string &) {}, [] {},
                         [](const boost::system::error_code &) {});
            io.run_for(std::chrono::milliseconds(50));
        }
        ok(true, "a reader destroyed before its end returns");
        close(w);
        close(r);
        close(fds[0]);
        close(fds[1]);
        unlink(fifo);
    }

    /* ExitSignals: the first signal calls the handler with its number,
       then the set is cleared and the default action is back. */
    {
        asio::io_context io;
        int got = 0;
        inn::ExitSignals sigs(io, {SIGUSR1, SIGUSR2},
                              [&](int signo) { got = signo; });
        raise(SIGUSR2);
        io.run_for(std::chrono::milliseconds(500));
        is_int(SIGUSR2, got, "exit signals: the handler gets the signal");
        struct sigaction sa;
        sigaction(SIGUSR1, nullptr, &sa);
        ok(sa.sa_handler == SIG_DFL,
           "exit signals: the default action is back after the first");
    }

    /* A second signal kills: check in a child. */
    child = fork();
    if (child < 0)
        sysbail("cannot fork");
    if (child == 0) {
        asio::io_context io;
        inn::ExitSignals sigs(io, {SIGTERM}, [&](int) { raise(SIGTERM); });
        raise(SIGTERM);
        io.run_for(std::chrono::seconds(2));
        _exit(0);
    }
    waitpid(child, &status, 0);
    ok(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM,
       "exit signals: a second signal kills");

    return 0;
}
