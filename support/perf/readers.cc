// readers: concurrent NNTP reader sessions for the performance comparison
// (doc/spec/performance-comparison.md, scenario W4).
//
//   readers <host> <port> <sessions> <articles-per-session> <seed> <group>...
//
// Each session connects, then for every group: GROUP, and OVER over the
// group's whole range (reading every line); then <articles-per-session>
// times: GROUP of a chosen group and ARTICLE of a chosen number in it,
// reading the whole article.  Choices come from the seed and the session
// number (raw std::mt19937 output, as in genarticles), so every build is
// asked the same things.  The sessions run in parallel threads.
//
// Prints per session "session N over_lines L articles A errors E", then
// "seconds S", and exits 0 only if no command failed.

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int connect_to(const char *host, const char *port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1;
    int fd = -1;
    for (addrinfo *a = res; a != nullptr; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd >= 0 && connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            break;
        if (fd >= 0)
            close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

class Conn {
public:
    explicit Conn(int fd) : fd_(fd) {}
    ~Conn()
    {
        if (fd_ >= 0)
            close(fd_);
    }
    Conn(const Conn &) = delete;
    Conn &operator=(const Conn &) = delete;

    bool line(std::string &out)
    {
        for (;;) {
            const auto nl = buf_.find('\n', pos_);
            if (nl != std::string::npos) {
                std::size_t end = nl;
                if (end > pos_ && buf_[end - 1] == '\r')
                    --end;
                out.assign(buf_, pos_, end - pos_);
                pos_ = nl + 1;
                return true;
            }
            buf_.erase(0, pos_);
            pos_ = 0;
            char chunk[65536];
            const ssize_t n = read(fd_, chunk, sizeof chunk);
            if (n <= 0)
                return false;
            buf_.append(chunk, static_cast<std::size_t>(n));
        }
    }

    bool send(std::string_view s)
    {
        while (!s.empty()) {
            const ssize_t n = write(fd_, s.data(), s.size());
            if (n <= 0)
                return false;
            s.remove_prefix(static_cast<std::size_t>(n));
        }
        return true;
    }

    // Send a command and return its status line.
    bool command(const std::string &cmd, std::string &status)
    {
        return send(cmd + "\r\n") && line(status);
    }

    // Read a multi-line block up to ".", returning the number of lines.
    long block()
    {
        std::string l;
        long n = 0;
        while (line(l)) {
            if (l == ".")
                return n;
            ++n;
        }
        return -1;
    }

private:
    int fd_;
    std::string buf_;
    std::size_t pos_ = 0;
};

struct Result {
    long over_lines = 0;
    long articles = 0;
    long errors = 0;
};

std::uint32_t below(std::mt19937 &e, std::uint32_t n)
{
    const std::uint32_t limit = UINT32_MAX - UINT32_MAX % n;
    std::uint32_t v;
    do {
        v = static_cast<std::uint32_t>(e());
    } while (v >= limit);
    return v % n;
}

struct Range {
    long low = 0;
    long high = -1;
};

// "211 count low high group" -> the range; false if not 211.
bool parse_group(const std::string &status, Range &r)
{
    if (status.rfind("211 ", 0) != 0)
        return false;
    long count = 0;
    return std::sscanf(status.c_str() + 4, "%ld %ld %ld", &count, &r.low,
                       &r.high) == 3;
}

void session(const char *host, const char *port, int id, long narticles,
             std::uint32_t seed, const std::vector<std::string> &groups,
             Result &res)
{
    const int fd = connect_to(host, port);
    if (fd < 0) {
        res.errors = 1;
        return;
    }
    Conn c(fd);
    std::string st;
    if (!c.line(st) || (st.rfind("200", 0) != 0 && st.rfind("201", 0) != 0)) {
        res.errors = 1;
        return;
    }
    std::vector<Range> ranges(groups.size());
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (!c.command("GROUP " + groups[g], st) ||
            !parse_group(st, ranges[g])) {
            ++res.errors;
            continue;
        }
        if (ranges[g].high < ranges[g].low)
            continue;
        if (!c.command("OVER " + std::to_string(ranges[g].low) + "-" +
                           std::to_string(ranges[g].high),
                       st) ||
            st.rfind("224", 0) != 0) {
            ++res.errors;
            continue;
        }
        const long n = c.block();
        if (n < 0)
            ++res.errors;
        else
            res.over_lines += n;
    }
    std::mt19937 e(seed + static_cast<std::uint32_t>(id));
    for (long i = 0; i < narticles; ++i) {
        const std::uint32_t g =
            below(e, static_cast<std::uint32_t>(groups.size()));
        const Range &r = ranges[g];
        if (r.high < r.low)
            continue;
        const long num =
            r.low + below(e, static_cast<std::uint32_t>(r.high - r.low + 1));
        if (!c.command("GROUP " + groups[g], st) ||
            st.rfind("211 ", 0) != 0 ||
            !c.command("ARTICLE " + std::to_string(num), st)) {
            ++res.errors;
            continue;
        }
        // A number can be missing only after expire; before it, every
        // number in the range exists, so anything but 220 is an error.
        if (st.rfind("220", 0) != 0) {
            ++res.errors;
            continue;
        }
        if (c.block() < 0)
            ++res.errors;
        else
            ++res.articles;
    }
    c.command("QUIT", st);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 7) {
        std::fputs("usage: readers <host> <port> <sessions> "
                   "<articles-per-session> <seed> <group>...\n",
                   stderr);
        return 2;
    }
    const int nsessions = std::atoi(argv[3]);
    const long narticles = std::atol(argv[4]);
    const auto seed =
        static_cast<std::uint32_t>(std::strtoul(argv[5], nullptr, 0));
    const std::vector<std::string> groups(argv + 6, argv + argc);

    std::vector<Result> results(static_cast<std::size_t>(nsessions));
    std::vector<std::thread> threads;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < nsessions; ++i)
        threads.emplace_back(session, argv[1], argv[2], i, narticles, seed,
                             std::cref(groups),
                             std::ref(results[static_cast<std::size_t>(i)]));
    for (auto &t : threads)
        t.join();
    const double secs = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - start)
                            .count();
    long errors = 0;
    for (int i = 0; i < nsessions; ++i) {
        const Result &r = results[static_cast<std::size_t>(i)];
        std::printf("session %d over_lines %ld articles %ld errors %ld\n", i,
                    r.over_lines, r.articles, r.errors);
        errors += r.errors;
    }
    std::printf("seconds %.3f\n", secs);
    return errors == 0 ? 0 : 1;
}
