/* Test helper: tcp-stdin program [arg ...] connects to a listener on the
   loopback address, makes the accepted end its standard input and standard
   output, and runs the program, so that getpeername(0, ...) works as it does
   under nnrpd.  Exits with the program's status. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: tcp-stdin program [arg ...]\n");
        return 2;
    }

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int client = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(addr);
    if (listener < 0 || client < 0
        || bind(listener, reinterpret_cast<sockaddr *>(&addr), len) != 0
        || listen(listener, 1) != 0
        || getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &len)
               != 0
        || connect(client, reinterpret_cast<sockaddr *>(&addr), len) != 0) {
        perror("tcp-stdin");
        return 2;
    }
    int accepted = accept(listener, nullptr, nullptr);
    if (accepted < 0) {
        perror("tcp-stdin accept");
        return 2;
    }

    /* The program reads its input from what we hold open on the other end:
       nothing to read, then end of file. */
    shutdown(client, SHUT_WR);
    dup2(accepted, 0);
    close(accepted);
    close(listener);
    execv(argv[1], argv + 1);
    perror("tcp-stdin exec");
    return 2;
}
