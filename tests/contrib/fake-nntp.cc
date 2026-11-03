/* Test helper: fake-nntp [banner] listens on the loopback address and prints
   its port on standard output, then serves one NNTP connection for
   contrib/newsresp: the banner (default "200 ready"), "335" to IHAVE, "437"
   after the article's "." line, and "205" to QUIT.  Every command received is
   written to standard error. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

static void
reply(int fd, const std::string &line)
{
    std::string out = line + "\r\n";
    if (write(fd, out.data(), out.size()) < 0)
        perror("fake-nntp write");
}

int
main(int argc, char *argv[])
{
    const char *banner = argc > 1 ? argv[1] : "200 ready";

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(addr);
    if (listener < 0
        || bind(listener, reinterpret_cast<sockaddr *>(&addr), len) != 0
        || listen(listener, 1) != 0
        || getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &len)
               != 0) {
        perror("fake-nntp");
        return 2;
    }
    printf("%d\n", ntohs(addr.sin_port));
    fflush(stdout);

    int fd = accept(listener, nullptr, nullptr);
    if (fd < 0) {
        perror("fake-nntp accept");
        return 2;
    }
    reply(fd, banner);

    std::string pending;
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        pending.append(buf, n);
        size_t eol;
        while ((eol = pending.find("\r\n")) != std::string::npos) {
            std::string cmd = pending.substr(0, eol);
            pending.erase(0, eol + 2);
            /* Show the command, but not the changing message-id. */
            fprintf(stderr, "%s\n",
                    cmd.compare(0, 5, "ihave") == 0 ? "ihave" : cmd.c_str());
            if (cmd.compare(0, 5, "ihave") == 0)
                reply(fd, "335 send it");
            else if (cmd == ".")
                reply(fd, "437 empty article");
            else if (cmd == "quit") {
                reply(fd, "205 bye");
                close(fd);
                return 0;
            }
        }
    }
    return 0;
}
