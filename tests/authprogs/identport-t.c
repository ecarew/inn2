/*
 * ident tests that need no real identd.  Each case starts a stand-in ident
 * server on a free 127.0.0.1 port, runs the uninstalled ident resolver with
 * -p <port> and the connection information nnrpd would send, and checks
 * both the query the server received and ident's output and exit status.
 */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <sys/wait.h>

#include "inn/messages.h"
#include "inn/network.h"
#include "tap/basic.h"

/* The path to the uninstalled ident resolver, as in ident-t.c. */
static const char ident[] = "../../authprogs/ident";

/* The ports ident is told about, so the expected query is known. */
#define CLIENT_PORT 4321
#define LOCAL_PORT  11119

/*
 * Run one case: serve reply (NULL means refuse the connection) to ident run
 * with options, and return ident's exit status.  Its output is stored in
 * out; *query_ok says whether the server saw the expected query.
 */
static int
run_case(const char *reply, const char *options, char *out, size_t outlen,
         bool *query_ok)
{
    socket_type fd, client;
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    unsigned short port;
    pid_t server = -1;
    char cmd[512], query[128], expected[64];
    FILE *in, *outfile;
    size_t got;
    int status, server_status;
    ssize_t n;

    *query_ok = false;
    fd = network_bind_ipv4(SOCK_STREAM, "127.0.0.1", 0);
    if (fd == INVALID_SOCKET || listen(fd, 1) < 0)
        sysbail("cannot listen");
    if (getsockname(fd, (struct sockaddr *) &sin, &len) < 0)
        sysbail("cannot getsockname");
    port = ntohs(sin.sin_port);
    if (reply == NULL) {
        /* Free the port so the connection is refused. */
        close(fd);
    } else {
        server = fork();
        if (server < 0)
            sysbail("cannot fork");
        if (server == 0) {
            client = accept(fd, NULL, NULL);
            if (client == INVALID_SOCKET)
                _exit(2);
            n = read(client, query, sizeof(query) - 1);
            query[n > 0 ? n : 0] = '\0';
            snprintf(expected, sizeof(expected), "%d , %d\r\n", CLIENT_PORT,
                     LOCAL_PORT);
            if (write(client, reply, strlen(reply)) < 0)
                _exit(2);
            close(client);
            _exit(strcmp(query, expected) == 0 ? 0 : 1);
        }
        close(fd);
    }

    in = fopen("identport.in", "w");
    if (in == NULL)
        sysbail("cannot create identport.in");
    fprintf(in, "ClientHost: localhost\r\nClientIP: 127.0.0.1\r\n");
    fprintf(in, "ClientPort: %d\r\nLocalIP: 127.0.0.1\r\n", CLIENT_PORT);
    fprintf(in, "LocalPort: %d\r\n.\r\n", LOCAL_PORT);
    fclose(in);
    snprintf(cmd, sizeof(cmd), "%s -p %hu %s <identport.in >identport.out 2>&1",
             ident, port, options);
    status = system(cmd);

    out[0] = '\0';
    outfile = fopen("identport.out", "r");
    if (outfile != NULL) {
        got = fread(out, 1, outlen - 1, outfile);
        out[got] = '\0';
        fclose(outfile);
    }
    if (server > 0) {
        if (waitpid(server, &server_status, 0) == server
            && WIFEXITED(server_status) && WEXITSTATUS(server_status) == 0)
            *query_ok = true;
    }
    unlink("identport.in");
    unlink("identport.out");
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int
main(void)
{
    char out[512];
    bool query_ok;
    int status;

    if (access("../data/etc/passwd", F_OK) == 0) {
        if (chdir("../data") < 0)
            sysbail("cannot chdir to ../data");
    } else if (access("data/etc/passwd", F_OK) == 0) {
        if (chdir("data") < 0)
            sysbail("cannot chdir to data");
    } else if (access("tests/data/etc/passwd", F_OK) == 0) {
        if (chdir("tests/data") < 0)
            sysbail("cannot chdir to tests/data");
    }
    if (access(ident, X_OK) < 0)
        skip_all("ident not built");

    plan(17);

    status = run_case("4321 , 11119 : USERID : UNIX : fakeuser\r\n", "", out,
                      sizeof(out), &query_ok);
    is_int(0, status, "USERID reply: exit status 0");
    is_string("User:fakeuser\r\n", out, "USERID reply: reports the user");
    ok(query_ok, "ident sends \"clientport , localport\"");

    status = run_case("4321 , 11119 : USERID : UNIX : fake@example.com\r\n",
                      "-t", out, sizeof(out), &query_ok);
    is_int(0, status, "-t: exit status 0");
    is_string("User:fake\r\n", out, "-t: truncates the domain");

    status = run_case("4321 , 11119 : USERID : UNIX : fake@example.com\r\n",
                      "", out, sizeof(out), &query_ok);
    is_int(0, status, "no -t: exit status 0");
    is_string("User:fake@example.com\r\n", out, "no -t: keeps the domain");

    status = run_case("4321 , 11119 : ERROR : NO-USER\r\n", "", out,
                      sizeof(out), &query_ok);
    ok(status != 0, "ERROR reply: fails");
    ok(strstr(out, "reported an error") != NULL,
       "ERROR reply: says the server reported an error");

    status = run_case("4321 , 11119 : OTHER : X\r\n", "", out, sizeof(out),
                      &query_ok);
    ok(status != 0, "unknown reply type: fails");
    ok(strstr(out, "not USERID") != NULL, "unknown reply type: says not USERID");

    status = run_case("garbage\r\n", "", out, sizeof(out), &query_ok);
    ok(status != 0, "malformed reply: fails");
    ok(strstr(out, "malformed response") != NULL,
       "malformed reply: says malformed");

    status = run_case("4321 , 11119 : USERID : UNIX : [encrypted]\r\n", "",
                      out, sizeof(out), &query_ok);
    ok(status != 0, "encrypted reply: fails");
    ok(strstr(out, "null or encrypted") != NULL,
       "encrypted reply: says null or encrypted");

    status = run_case(NULL, "", out, sizeof(out), &query_ok);
    ok(status != 0, "refused connection: fails");
    ok(strstr(out, "does not accept ident") != NULL,
       "refused connection: says so");

    return 0;
}
