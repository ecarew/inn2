/* newsresp.c - EUnet - bilse */

/*
 * Various bug fixes, code and documentation improvements
 * in 2000-2003, 2014, 2017, 2021, 2024, 2026.
 *
 * From: Koen De Vleeschauwer <koen@eu.net>
 * Subject: Re: innfeed-users: innfeed: measuring server response time
 * To: jeff.garzik@spinne.com (Jeff Garzik)
 * Date: Tue, 13 May 1997 16:33:27 +0200 (MET DST)
 * Cc: innfeed-users@vix.com
 *
 * > Is there an easy way to measure server response time, and print it out
 * > on the innfeed status page?  Cyclone's nntpTime measures login banner
 * > response time and an article add and lookup operation.
 * >
 * > It seems to me that innfeed could do something very similar.  It could
 * > very easily sample gettimeofday() or Time.Now to determine a remote
 * > server's average response time for lookups, lookup failures, article
 * > send throughput, whatever.
 * >
 * > These statistics might be invaluable to developers creating advanced
 * > connection and article delivery algorithms.  If I knew, for example,
 * > that a site's article send/save throughput was really fast, but history
 * > lookups were really slow, my algorithm could reserve a channel or two
 * > for TAKETHIS-only use.
 *
 * We use a stand-alone program which opens up an additional nntp channel
 * from time to time and takes a peek at the various response times.
 * It's also interesting to tune one's own box.
 * I've included the source code; please consider this supplied 'as is';
 * bugs and features alike. SunOS, Solaris and Irix ought to be ok;
 * eg. gcc -traditional -o newsresp ./newsresp.c -lnsl -lsocket on S0laris.
 * If a host has an uncommonly long banner you may have to change a constant
 * somewhere; forget. Please note one has to interpret the output;
 * eg. whether one is measuring rtt or history lookup time.
 *
 * Basic usage is (-p gives another port than 119):
 * news 1 % newsresp -n 5 news.eu.net
 * ---------------------------------
 * news.eu.net is 134.222.90.2 port 119
 *  elap  diff
 *   0.0   0.0  Connecting ...
 *   0.0   0.0  OK, waiting for prompt
 *   0.0   0.0  <<< 200 EU.net InterNetNews server INN 1.5.1 17-Dec-1996
 *   0.0   0.0  >>> ihave <244796399@a>
 *   0.0   0.0  <<< 335
 *   0.0   0.0  >>> .
 *   0.0   0.0  <<< 437 Empty article
 *   0.0   0.0  >>> ihave <244796398@a>
 *   0.0   0.0  <<< 335
 *   0.0   0.0  >>> .
 *   0.0   0.0  <<< 437 Empty article
 *   0.0   0.0  >>> ihave <244796397@a>
 *   0.0   0.0  <<< 335
 *   0.0   0.0  >>> .
 *   0.0   0.0  <<< 437 Empty article
 *   0.0   0.0  >>> ihave <244796396@a>
 *   0.1   0.0  <<< 335
 *   0.1   0.0  >>> .
 *   0.1   0.0  <<< 437 Empty article
 *   0.1   0.0  >>> ihave <244796395@a>
 *   0.1   0.0  <<< 335
 *   0.1   0.0  >>> .
 *   0.1   0.0  <<< 437 Empty article
 *   0.1   0.0  >>> quit
 *   0.1   0.0  <<< 205 .
 */

#include "portable/system.h"

#include "portable/socket.h"

#include <errno.h>
#include <netdb.h>
#ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#endif
#include <time.h>

#include <string>

#define NNTPPORT "119"

/* When the connection started, and the elapsed and last-step times in
   milliseconds. */
static unsigned long start, elapsed, diff;

static unsigned long
now_ms(void)
{
    struct timeval now;

    gettimeofday(&now, NULL);
    return now.tv_sec * 1000UL + now.tv_usec / 1000;
}

static void
ptime(void)
{
    diff = elapsed;
    elapsed = now_ms() - start;
    diff = elapsed - diff;
    printf("%5.1f %5.1f  ", ((double) elapsed) / 1000.0,
           ((double) diff) / 1000.0);
}

static void
error(const char *what)
{
    ptime();
    fflush(stdout);
    perror(what);
}

static void
ierror(const char *how, const char *what)
{
    printf("Expected %s, received %s; bailing out.\n", how, what);
}

/* Shorten a server response to one line of at most 55 characters, with
   carriage returns made into spaces.  bread is how many bytes buffer holds;
   there is room for its terminating null. */
static void
massagebuff(int bread, char *buffer)
{
    if (bread > 55)
        strcpy(buffer + 55, " [...]\n");
    else
        buffer[bread] = '\0';
    for (char *p = buffer; *p != '\0'; p++)
        if (*p == '\r')
            *p = ' ';
}

/* Read the server's reply into buf, shortened for display, and show it.
   Returns false on a read error. */
static bool
get_reply(int sock, char *buf, size_t size)
{
    ssize_t bread = read(sock, buf, size - 1);

    if (bread < 0) {
        error("read socket");
        return false;
    }
    massagebuff(bread, buf);
    ptime();
    printf("<<< %s", buf);
    return true;
}

static bool
send_line(int sock, const char *line)
{
    size_t len = strlen(line);

    ptime();
    printf(">>> %s", line);
    if (write(sock, line, len) != static_cast<ssize_t>(len)) {
        error("write socket");
        return false;
    }
    return true;
}

static bool
punt(int sock, unsigned numart)
{
    char buf[1024];
    char ihave[64];

    printf(" elap  diff\n");
    diff = elapsed = 0;
    start = now_ms();

    ptime();
    printf("OK, waiting for prompt\n");
    if (!get_reply(sock, buf, sizeof(buf)))
        return false;
    if (strncmp(buf, "200", 3) != 0 && strncmp(buf, "201", 3) != 0) {
        ierror("200 or 201", buf);
        return false;
    }

    do {
        snprintf(ihave, sizeof(ihave), "ihave <%lu@a>\r\n",
                 start + numart);
        if (!send_line(sock, ihave) || !get_reply(sock, buf, sizeof(buf)))
            return false;
        if (strncmp(buf, "335", 3) != 0 && strncmp(buf, "435", 3) != 0) {
            ierror("335 or 435", buf);
            return false;
        }

        if (strncmp(buf, "335", 3) == 0) {
            if (!send_line(sock, ".\r\n")
                || !get_reply(sock, buf, sizeof(buf)))
                return false;
            if (strncmp(buf, "437", 3) != 0 && strncmp(buf, "235", 3) != 0) {
                ierror("437 or 235", buf);
                return false;
            }
        }
    } while (--numart != 0);

    if (!send_line(sock, "quit\r\n") || !get_reply(sock, buf, sizeof(buf)))
        return false;
    if (strncmp(buf, "205", 3) != 0) {
        ierror("205", buf);
        return false;
    }
    return true;
}

/* Connect to host, trying each address it has.  Returns the socket, or -1
   with the error shown; the address connected to is printed. */
static int
open_connection(const char *host, const char *port)
{
    struct addrinfo hints, *res, *ai;
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "can't find %s: %s\n", host, gai_strerror(rc));
        return -1;
    }

    for (ai = res; ai != NULL; ai = ai->ai_next) {
        char addr[NI_MAXHOST];

        if (getnameinfo(ai->ai_addr, ai->ai_addrlen, addr, sizeof(addr), NULL,
                        0, NI_NUMERICHOST)
            != 0)
            strlcpy(addr, "?", sizeof(addr));
        printf("---------------------------------\n%s is %s port %s\n", host,
               addr, port);
        printf(" elap  diff\n");
        start = now_ms();
        elapsed = diff = 0;
        ptime();
        printf("Connecting ...\n");

        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock >= 0 && connect(sock, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        error(sock < 0 ? "socket" : "connect");
        if (sock >= 0)
            close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;
}

int
main(int argc, char *argv[])
{
    int errflg = 0, c;
    bool status = true;
    unsigned numart = 1;
    std::string port = NNTPPORT;

    while ((c = getopt(argc, argv, "n:p:")) != -1)
        switch (c) {
        case 'n': {
            char *end;
            unsigned long n = strtoul(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || n == 0 || n > 1000000)
                errflg++;
            else
                numart = static_cast<unsigned>(n);
            break;
        }
        case 'p':
            port = optarg;
            break;
        default:
            errflg++;
        }
    if (optind == argc)
        errflg++;
    if (errflg) {
        fprintf(stderr, "Usage: %s [-n articles] [-p port] host ...\n",
                argv[0]);
        exit(1);
    }

    for (char **whoP = argv + optind; *whoP != NULL; whoP++) {
        int sock = open_connection(*whoP, port.c_str());
        if (sock < 0) {
            status = false;
            continue;
        }
        if (!punt(sock, numart))
            status = false;
        close(sock);
    }
    exit(status ? 0 : 1);
}
