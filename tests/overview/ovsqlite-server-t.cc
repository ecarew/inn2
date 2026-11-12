/*
 * Test suite for ovsqlite-server's event loop, through its wire protocol
 * (storage/ovsqlite/ovsqlite-private.h): many clients at once, requests
 * split across writes, pipelined requests, clients that disconnect
 * mid-request, fatal errors, the transaction commit timer, and shutdown by
 * signal.  The server is the one OVSQLITE_SERVER names, run with -d.
 */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <vector>

#include "inn/libinn.h"
#include "tap/basic.h"

#ifdef HAVE_SQLITE3
#    include "ovsqlite/ovsqlite-private.h"
#endif

#if defined(HAVE_SQLITE3) && defined(HAVE_UNIX_DOMAIN_SOCKETS)

namespace {

std::string dir;
pid_t server = -1;

void
writeFile(const std::string &path, const std::string &text)
{
    FILE *f = fopen(path.c_str(), "w");
    if (f == nullptr)
        sysbail("cannot create %s", path.c_str());
    fputs(text.c_str(), f);
    fclose(f);
}

void
sleepMs(int ms)
{
    poll(nullptr, 0, ms);
}

/* Start the server and wait for its socket. */
void
startServer()
{
    const char *path = getenv("OVSQLITE_SERVER");
    std::string sock = dir + "/" OVSQLITE_SERVER_SOCKET;

    unlink(sock.c_str());
    server = fork();
    if (server < 0)
        sysbail("cannot fork");
    if (server == 0) {
        std::string log = dir + "/server.log";
        int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
        }
        execl(path, "ovsqlite-server", "-d", (char *) nullptr);
        _exit(127);
    }
    for (int i = 0; i < 100; i++) {
        struct stat st;

        if (stat(sock.c_str(), &st) == 0)
            return;
        if (waitpid(server, nullptr, WNOHANG) == server)
            bail("server exited at startup; see %s/server.log", dir.c_str());
        sleepMs(50);
    }
    bail("server socket did not appear");
}

/* Signal the server and return its exit status, or -1 after 10 s. */
int
stopServer(int sig)
{
    int status;

    kill(server, sig);
    for (int i = 0; i < 200; i++) {
        if (waitpid(server, &status, WNOHANG) == server) {
            server = -1;
            return status;
        }
        sleepMs(50);
    }
    kill(server, SIGKILL);
    waitpid(server, &status, 0);
    server = -1;
    return -1;
}

int
connectServer()
{
    struct sockaddr_un sa;
    std::string sock = dir + "/" OVSQLITE_SERVER_SOCKET;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0)
        sysbail("cannot create socket");
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strlcpy(sa.sun_path, sock.c_str(), sizeof sa.sun_path);
    if (connect(fd, (struct sockaddr *) &sa, SUN_LEN(&sa)) != 0)
        sysbail("cannot connect to %s", sock.c_str());
    return fd;
}

/* A request or response: native-order fields, no padding. */
struct Message {
    std::string bytes;

    explicit Message(uint8_t code) : bytes(4, '\0')
    {
        bytes += static_cast<char>(code);
    }
    template <class T>
    Message &
    put(T value)
    {
        bytes.append(reinterpret_cast<const char *>(&value), sizeof value);
        return *this;
    }
    Message &
    name(const std::string &s)
    {
        put(static_cast<uint16_t>(s.size()));
        bytes += s;
        return *this;
    }
    const std::string &
    done()
    {
        uint32_t len = bytes.size();
        memcpy(&bytes[0], &len, sizeof len);
        return bytes;
    }
};

bool
sendAll(int fd, const std::string &data)
{
    return xwrite(fd, data.data(), data.size()) == (ssize_t) data.size();
}

/* Read exactly n bytes, waiting at most 5 s; false on EOF or timeout. */
bool
readExact(int fd, char *buf, size_t n)
{
    while (n > 0) {
        struct pollfd p = {fd, POLLIN, 0};

        if (poll(&p, 1, 5000) <= 0)
            return false;
        ssize_t got = read(fd, buf, n);
        if (got <= 0)
            return false;
        buf += got;
        n -= got;
    }
    return true;
}

/* Read one response; its code, or -1.  The body goes to *body. */
int
readResponse(int fd, std::string *body = nullptr)
{
    uint32_t len;
    char code;

    if (!readExact(fd, reinterpret_cast<char *>(&len), sizeof len)
        || len < 5 || !readExact(fd, &code, 1))
        return -1;
    std::string rest(len - 5, '\0');
    if (len > 5 && !readExact(fd, &rest[0], rest.size()))
        return -1;
    if (body != nullptr)
        *body = rest;
    return static_cast<uint8_t>(code);
}

/* True if the server closes fd within 5 s. */
bool
closedByServer(int fd)
{
    char c;
    struct pollfd p = {fd, POLLIN, 0};

    return poll(&p, 1, 5000) == 1 && read(fd, &c, 1) == 0;
}

const std::string &
hello()
{
    static const std::string m = Message(request_hello)
                                     .put(static_cast<uint32_t>(
                                         OVSQLITE_PROTOCOL_VERSION))
                                     .put(static_cast<uint32_t>(0))
                                     .done();

    return m;
}

int
helloClient()
{
    int fd = connectServer();

    if (!sendAll(fd, hello()) || readResponse(fd) != response_ok)
        bail("hello failed");
    return fd;
}

std::string
addGroup(const std::string &group, uint64_t low, uint64_t high)
{
    return Message(request_add_group)
        .name(group)
        .put(low)
        .put(high)
        .name("y")
        .done();
}

std::string
getGroup(const std::string &group)
{
    return Message(request_get_groupinfo).name(group).done();
}

std::string
addArticle(const std::string &group, uint64_t artnum)
{
    Message m(request_add_article);
    std::string over = "Subject\tFrom\tDate\t<id@x>\t\t1\t1";
    char token[18];

    memset(token, static_cast<char>(artnum), sizeof token);
    m.name(group).put(artnum).put(static_cast<int64_t>(time(nullptr)))
        .put(static_cast<int64_t>(0));
    m.bytes.append(token, sizeof token);
    m.put(static_cast<uint32_t>(over.size()));
    m.bytes += over;
    return m.done();
}

std::string
getArticle(const std::string &group, uint64_t artnum)
{
    return Message(request_get_artinfo).name(group).put(artnum).done();
}

/* The low and high water marks in a response_groupinfo body. */
bool
marks(const std::string &body, uint64_t low, uint64_t high)
{
    uint64_t l, h;

    if (body.size() < 16)
        return false;
    memcpy(&l, body.data(), 8);
    memcpy(&h, body.data() + 8, 8);
    return l == low && h == high;
}

} // namespace

int
main()
{
    if (getenv("OVSQLITE_SERVER") == nullptr)
        skip_all("OVSQLITE_SERVER not set");
    signal(SIGPIPE, SIG_IGN);

    char tmpl[] = "/tmp/ovsqlite-server-XXXXXX";
    if (mkdtemp(tmpl) == nullptr)
        sysbail("cannot create temporary directory");
    dir = tmpl;
    writeFile(dir + "/inn.conf",
              "domain: news.example.com\n"
              "pathhost: inn.example.com\n"
              "mta: \"/usr/sbin/sendmail -oi %s\"\n"
              "hismethod: hisv6\n"
              "ovmethod: ovsqlite\n"
              "enableoverview: true\n"
              "pathnews: " + dir + "\n"
              "pathoverview: " + dir + "\n"
              "pathrun: " + dir + "\n"
              "pathetc: " + dir + "\n"
              "pathdb: " + dir + "\n"
              "pathlog: " + dir + "\n"
              "pathtmp: " + dir + "\n");
    /* A long enough time limit that the shutdown test's article is still
       uncommitted when the signal arrives. */
    writeFile(dir + "/ovsqlite.conf",
              "walmode: true\n"
              "compress: false\n"
              "transrowlimit: 10000\n"
              "transtimelimit: 1.0\n");
    setenv("INNCONF", (dir + "/inn.conf").c_str(), 1);

    plan(23);
    startServer();
    ok(true, "server started");

    /* Many clients at once: all connect, then all say hello, then all read
       their responses. */
    const int nclients = 40;
    std::vector<int> fds;
    for (int i = 0; i < nclients; i++)
        fds.push_back(connectServer());
    bool sent = true;
    for (int fd : fds)
        sent = sendAll(fd, hello()) && sent;
    int good = 0;
    for (int fd : fds)
        if (readResponse(fd) == response_ok)
            good++;
    ok(sent, "hello sent on %d connections", nclients);
    is_int(nclients, good, "all %d clients got response_ok", nclients);

    /* Interleaved requests: each client adds its own group, then each reads
       back its own. */
    for (int i = 0; i < nclients; i++)
        sendAll(fds[i], addGroup("test.g" + std::to_string(i), 1, i + 1));
    good = 0;
    for (int fd : fds)
        if (readResponse(fd) == response_ok)
            good++;
    is_int(nclients, good, "interleaved add_group on every client");
    for (int i = 0; i < nclients; i++)
        sendAll(fds[i], getGroup("test.g" + std::to_string(i)));
    good = 0;
    for (int i = 0; i < nclients; i++) {
        std::string body;
        if (readResponse(fds[i], &body) == response_groupinfo
            && marks(body, 1, i + 1))
            good++;
    }
    is_int(nclients, good, "each client reads back its own group");
    for (int fd : fds)
        close(fd);

    /* A request split across writes, byte by byte for the header. */
    int a = helloClient();
    std::string req = getGroup("test.g3");
    bool split = true;
    for (size_t i = 0; i < req.size(); i++) {
        split = sendAll(a, req.substr(i, 1)) && split;
        if (i < 6)
            sleepMs(20);
    }
    std::string body;
    ok(split && readResponse(a, &body) == response_groupinfo
           && marks(body, 1, 4),
       "request split across writes");

    /* Two requests in one write: two responses, in order. */
    sendAll(a, getGroup("test.g5") + getGroup("no.such.group"));
    int first = readResponse(a, &body);
    bool firstGood = first == response_groupinfo && marks(body, 1, 6);
    ok(firstGood, "pipelined: first response");
    is_int(response_no_group, readResponse(a), "pipelined: second response");

    /* Clients that disconnect mid-request: in the length, and after the
       header with the body missing. */
    int b = helloClient();
    sendAll(b, req.substr(0, 2));
    close(b);
    b = helloClient();
    sendAll(b, req.substr(0, req.size() - 3));
    close(b);
    /* One that disconnects with a response pending. */
    b = helloClient();
    sendAll(b, getGroup("test.g1"));
    close(b);
    sendAll(a, getGroup("test.g7"));
    ok(readResponse(a, &body) == response_groupinfo && marks(body, 1, 8),
       "server still answers after three clients dropped mid-request");

    /* Fatal errors close the connection after the response. */
    int c = connectServer();
    sendAll(c, getGroup("test.g1"));
    is_int(response_wrong_state, readResponse(c), "request before hello");
    ok(closedByServer(c), "connection closed after wrong_state");
    close(c);
    c = helloClient();
    uint32_t huge = 0x100000;
    sendAll(c, std::string(reinterpret_cast<char *>(&huge), 4) + "x");
    is_int(response_oversized, readResponse(c), "oversized request");
    ok(closedByServer(c), "connection closed after oversized");
    close(c);
    c = helloClient();
    uint32_t tiny = 4;
    sendAll(c, std::string(reinterpret_cast<char *>(&tiny), 4) + "x");
    is_int(response_bad_request, readResponse(c), "undersized request");
    close(c);
    sendAll(a, getGroup("test.g9"));
    ok(readResponse(a, &body) == response_groupinfo && marks(body, 1, 10),
       "other clients unaffected by fatal errors");

    /* The commit timer: an article is committed within transtimelimit
       even with no further requests; SIGKILL then loses nothing. */
    sendAll(a, addArticle("test.g0", 1));
    is_int(response_ok, readResponse(a), "add_article");
    sleepMs(2500);
    close(a);
    int status = stopServer(SIGKILL);
    ok(WIFSIGNALED(status), "server killed");
    startServer();
    a = helloClient();
    sendAll(a, getArticle("test.g0", 1));
    is_int(response_artinfo, readResponse(a),
           "article committed by the timer survives SIGKILL");

    /* Shutdown by signal: an uncommitted article is committed, the server
       exits 0 and removes its pid file. */
    sendAll(a, addArticle("test.g0", 2));
    is_int(response_ok, readResponse(a), "add_article before SIGTERM");
    status = stopServer(SIGTERM);
    ok(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
       "SIGTERM: server exits 0 (status %d)", status);
    ok(closedByServer(a), "SIGTERM: client connection closed");
    close(a);
    struct stat st;
    ok(stat((dir + "/" OVSQLITE_SERVER_PIDFILE).c_str(), &st) != 0,
       "SIGTERM: pid file removed");
    startServer();
    a = helloClient();
    sendAll(a, getArticle("test.g0", 2));
    is_int(response_artinfo, readResponse(a),
           "article pending at SIGTERM was committed");
    close(a);
    stopServer(SIGTERM);

    std::string cmd = "rm -rf '" + dir + "'";
    if (system(cmd.c_str()) != 0)
        diag("cannot remove %s", dir.c_str());
    return 0;
}

#else

int
main()
{
    skip_all("needs SQLite and Unix-domain sockets");
}

#endif
