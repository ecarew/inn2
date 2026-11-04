// feeder: stream a genarticles file to an NNTP server, as a peer's feed
// does (doc/spec/performance-comparison.md, scenarios W1 and W2).
//
//   feeder takethis <host> <port> <file>   send every article (W1)
//   feeder check    <host> <port> <file>   offer every Message-ID (W2)
//
// MODE STREAM, then the commands are pipelined: one thread writes, the
// other reads the replies, so the server is never idle waiting for a
// round trip.  Prints the counts of each reply code and the wall time,
// one "key value" per line, and exits 0 only if every command got a
// reply.

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <thread>

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

class LineReader {
public:
    explicit LineReader(int fd) : fd_(fd) {}

    // Next line without its CRLF; false at end of stream.
    bool next(std::string &line)
    {
        for (;;) {
            const auto nl = buf_.find('\n', pos_);
            if (nl != std::string::npos) {
                std::size_t end = nl;
                if (end > pos_ && buf_[end - 1] == '\r')
                    --end;
                line.assign(buf_, pos_, end - pos_);
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

private:
    int fd_;
    std::string buf_;
    std::size_t pos_ = 0;
};

bool write_all(int fd, std::string_view s)
{
    while (!s.empty()) {
        const ssize_t n = write(fd, s.data(), s.size());
        if (n <= 0)
            return false;
        s.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
}

int usage()
{
    std::fputs("usage: feeder takethis|check <host> <port> <file>\n",
               stderr);
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 5)
        return usage();
    const std::string_view mode = argv[1];
    const bool takethis = mode == "takethis";
    if (!takethis && mode != "check")
        return usage();
    std::ifstream in(argv[4], std::ios::binary);
    if (!in) {
        std::perror(argv[4]);
        return 1;
    }
    const int fd = connect_to(argv[2], argv[3]);
    if (fd < 0) {
        std::fprintf(stderr, "feeder: cannot connect to %s:%s\n", argv[2],
                     argv[3]);
        return 1;
    }
    LineReader reader(fd);
    std::string line;
    if (!reader.next(line) || (line.rfind("200", 0) != 0 &&
                               line.rfind("201", 0) != 0)) {
        std::fprintf(stderr, "feeder: greeting: %s\n", line.c_str());
        return 1;
    }
    if (!write_all(fd, "MODE STREAM\r\n") || !reader.next(line) ||
        line.rfind("203", 0) != 0) {
        std::fprintf(stderr, "feeder: MODE STREAM: %s\n", line.c_str());
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    long sent = 0;
    std::map<std::string, long> codes;
    long replies = 0;

    // Counts replies until QUIT's 205, which the writer sends last, so
    // the thread never waits on a reply that will not come.
    std::thread replies_thread([&] {
        std::string r;
        while (reader.next(r)) {
            if (r.rfind("205", 0) == 0)
                break;
            ++codes[r.substr(0, 3)];
            ++replies;
        }
    });

    // Records: "#! <id> <n>" then the dot-stuffed article ending ".\r\n".
    std::string rec;
    std::string out;
    std::string id;
    bool ok = true;
    while (ok && std::getline(in, rec)) {
        if (rec.rfind("#! ", 0) != 0)
            continue;
        id = rec.substr(3, rec.find(' ', 3) - 3);
        out.clear();
        if (takethis) {
            out = "TAKETHIS " + id + "\r\n";
            while (std::getline(in, rec)) {
                out += rec;
                out += '\n';
                if (rec == ".\r")
                    break;
            }
        } else {
            out = "CHECK " + id + "\r\n";
        }
        ok = write_all(fd, out);
        ++sent;
    }
    // Unblock the reader if the server dropped the connection.
    if (!ok || !write_all(fd, "QUIT\r\n"))
        shutdown(fd, SHUT_RD);
    replies_thread.join();
    const double secs = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - start)
                            .count();
    close(fd);

    std::printf("sent %ld\nreplies %ld\nseconds %.3f\n", sent,
                replies, secs);
    for (const auto &[code, n] : codes)
        std::printf("code_%s %ld\n", code.c_str(), n);
    return ok && replies == sent ? 0 : 1;
}
