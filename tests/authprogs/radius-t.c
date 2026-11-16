/*
 * radius tests with a stand-in RADIUS server.  For each case a child process
 * serves UDP on a free 127.0.0.1 port: it decodes the Access-Request's
 * User-Name and User-Password (RFC 2865: password XORed with
 * MD5(secret + request authenticator), chained per 16 bytes), checks them
 * against the expected values, and answers Access-Accept or Access-Reject
 * with the correct response authenticator
 * (MD5(code + id + length + request authenticator + attributes + secret)).
 * The uninstalled radius resolver is run against it with -f and a generated
 * configuration file.  Two cases wait: a server that never answers (radius
 * times out on each of its three tries), and a normal case with radius's
 * socket above FD_SETSIZE, which a select()-based wait cannot take.
 */

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>

#include "inn/md5.h"
#include "inn/messages.h"
#include "inn/network.h"
#include "tap/basic.h"

static const char radius[] = "../../authprogs/radius";
static const char secret[] = "testing123";

enum reply_mode {
    REPLY_CHECK,    /* accept if name and password match, else reject */
    REPLY_BADSIG,   /* first a reply with a bad authenticator, then CHECK */
    REPLY_SILENT,   /* read radius's three tries and never answer */
};

/* When set, run_case fills descriptors up past FD_SETSIZE before running
   radius, so the socket radius makes lands above it. */
static bool fill_fds = false;

/* Build and send a reply to the request in req (length reqlen). */
static void
send_reply(socket_type fd, const struct sockaddr_in *to, unsigned char code,
           const unsigned char *req, const char *key)
{
    unsigned char reply[20], buf[20 + 64];
    unsigned char digest[MD5_DIGESTSIZE];
    size_t keylen = strlen(key);

    reply[0] = code;
    reply[1] = req[1];
    reply[2] = 0;
    reply[3] = 20;
    memcpy(buf, reply, 4);
    memcpy(buf + 4, req + 4, 16);
    memcpy(buf + 20, key, keylen);
    md5_hash(buf, 20 + keylen, digest);
    memcpy(reply + 4, digest, 16);
    sendto(fd, reply, sizeof(reply), 0, (const struct sockaddr *) to,
           sizeof(*to));
}

/*
 * Serve one request.  Returns, as the child's exit status, 0 if the name and
 * password received were the expected ones, 1 if not, 2 on error.
 */
static int
serve(socket_type fd, const char *want_name, const char *want_pass,
      enum reply_mode mode)
{
    unsigned char req[4096], buf[64 + 16];
    int tries;
    unsigned char digest[MD5_DIGESTSIZE];
    char name[256] = "", pass[256] = "";
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    ssize_t n;
    size_t i, j, alen, len, keylen = strlen(secret);
    bool match;

    if (mode == REPLY_SILENT) {
        /* radius sends one request per try; count them, never answer. */
        alarm(60);
        for (tries = 0; tries < 3; tries++) {
            n = recvfrom(fd, req, sizeof(req), 0, (struct sockaddr *) &from,
                         &fromlen);
            if (n < 20 || req[0] != 1)
                return 2;
        }
        return 0;
    }
    n = recvfrom(fd, req, sizeof(req), 0, (struct sockaddr *) &from, &fromlen);
    if (n < 20 || req[0] != 1)
        return 2;
    len = (size_t) req[2] << 8 | req[3];
    for (i = 20; i + 2 <= len; i += alen) {
        alen = req[i + 1];
        if (alen < 2 || i + alen > len)
            return 2;
        if (req[i] == 1 && alen - 2 < sizeof(name)) {
            memcpy(name, req + i + 2, alen - 2);
            name[alen - 2] = '\0';
        } else if (req[i] == 2 && alen - 2 < sizeof(pass)) {
            /* Decode: block k is XORed with MD5(secret + previous cipher
               block), the first block with the request authenticator. */
            memcpy(buf, secret, keylen);
            memcpy(buf + keylen, req + 4, 16);
            for (j = 0; j < alen - 2; j++) {
                if (j % 16 == 0) {
                    md5_hash(buf, keylen + 16, digest);
                    memcpy(buf + keylen, req + i + 2 + j, 16);
                }
                pass[j] = (char) (req[i + 2 + j] ^ digest[j % 16]);
            }
            pass[alen - 2] = '\0';
        }
    }
    match = strcmp(name, want_name) == 0 && strcmp(pass, want_pass) == 0;
    if (mode == REPLY_BADSIG)
        send_reply(fd, &from, 2, req, "not-the-secret");
    send_reply(fd, &from, match ? 2 : 3, req, secret);
    return match ? 0 : 1;
}

/*
 * Run radius for user/password against a server expecting want_name and
 * want_pass.  extra is added to the server block of the configuration.
 * Returns radius's exit status; its output goes to out, and *server_ok says
 * whether the server received the expected name and password.
 */
static int
run_case(const char *user, const char *password, const char *want_name,
         const char *want_pass, const char *extra, enum reply_mode mode,
         char *out, size_t outlen, bool *server_ok)
{
    socket_type fd;
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    pid_t server;
    FILE *f;
    char cmd[512];
    size_t got;
    int status, sstatus, fd_fill = -1, high = -1;

    fd = network_bind_ipv4(SOCK_DGRAM, "127.0.0.1", 0);
    if (fd == INVALID_SOCKET)
        sysbail("cannot bind UDP socket");
    if (getsockname(fd, (struct sockaddr *) &sin, &len) < 0)
        sysbail("cannot getsockname");
    server = fork();
    if (server < 0)
        sysbail("cannot fork");
    if (server == 0)
        _exit(serve(fd, want_name, want_pass, mode));
    close(fd);

    f = fopen("radius.conf", "w");
    if (f == NULL)
        sysbail("cannot create radius.conf");
    fprintf(f, "server test {\n  radhost: 127.0.0.1\n  radport: %hu\n",
            ntohs(sin.sin_port));
    fprintf(f, "  lochost: 127.0.0.1\n  secret: %s\n%s}\n", secret, extra);
    fclose(f);
    f = fopen("radius.in", "w");
    if (f == NULL)
        sysbail("cannot create radius.in");
    fprintf(f, "ClientAuthname: %s\r\nClientPassword: %s\r\n.\r\n", user,
            password);
    fclose(f);
    /* Under a 60 s alarm: a radius that never times out (as the select()
       version did with its socket above FD_SETSIZE, a silent server and
       macOS) fails the case instead of hanging the test. */
    snprintf(cmd, sizeof(cmd),
             "perl -e 'alarm shift; exec @ARGV' 60 %s -f radius.conf "
             "<radius.in >radius.out 2>&1",
             radius);
    if (fill_fds) {
        /* Inherited by radius: its socket gets the lowest free descriptor,
           above these. */
        high = FD_SETSIZE + 10;
        fd_fill = open("/dev/null", O_RDONLY);
        if (fd_fill < 0)
            sysbail("cannot open /dev/null");
        for (int i = fd_fill + 1; i <= high; i++)
            if (fcntl(i, F_GETFD) < 0 && dup2(fd_fill, i) < 0)
                sysbail("cannot fill descriptor %d", i);
    }
    status = system(cmd);
    if (fill_fds)
        for (int i = fd_fill; i <= high; i++)
            close(i);

    out[0] = '\0';
    f = fopen("radius.out", "r");
    if (f != NULL) {
        got = fread(out, 1, outlen - 1, f);
        out[got] = '\0';
        fclose(f);
    }
    *server_ok = waitpid(server, &sstatus, 0) == server && WIFEXITED(sstatus)
                 && WEXITSTATUS(sstatus) == 0;
    unlink("radius.conf");
    unlink("radius.in");
    unlink("radius.out");
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int
main(void)
{
    char out[1024];
    bool server_ok;
    time_t started;
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
    if (access(radius, X_OK) < 0)
        skip_all("radius not built");
    if (setenv("INNCONF", "etc/inn.conf", 1) < 0)
        sysbail("cannot set INNCONF");

    plan(23);

    status = run_case("alice", "secret1", "alice", "secret1", "", REPLY_CHECK,
                      out, sizeof(out), &server_ok);
    is_int(0, status, "right password: exit status 0");
    is_string("User:alice\r\n", out, "right password: reports the user");
    ok(server_ok, "right password: server decoded name and password");

    status = run_case("alice", "wrong", "alice", "secret1", "", REPLY_CHECK,
                      out, sizeof(out), &server_ok);
    ok(status != 0, "wrong password: fails");
    ok(strstr(out, "password doesn't match") != NULL,
       "wrong password: says the password doesn't match");
    ok(!server_ok, "wrong password: server saw the wrong password");

    status = run_case("bob", "a-password-longer-than-16-bytes",
                      "bob", "a-password-longer-than-16-bytes", "",
                      REPLY_CHECK, out, sizeof(out), &server_ok);
    is_int(0, status, "long password: exit status 0");
    ok(server_ok, "long password: chained encryption decodes");

    status = run_case("carol", "pw", "pre-carol@realm", "pw",
                      "  prefix: pre-\n  suffix: @realm\n", REPLY_CHECK, out,
                      sizeof(out), &server_ok);
    is_int(0, status, "prefix and suffix: exit status 0");
    ok(server_ok, "prefix and suffix: server sees pre-carol@realm");
    is_string("User:carol\r\n", out, "prefix and suffix: reports bare user");

    status = run_case("dave@other", "pw", "dave@other", "pw",
                      "  suffix: @realm\n", REPLY_CHECK, out, sizeof(out),
                      &server_ok);
    ok(server_ok, "suffix not added to a name with @");

    status = run_case("erin", "pw", "erin", "pw", "", REPLY_BADSIG, out,
                      sizeof(out), &server_ok);
    is_int(0, status, "bad authenticator ignored, good reply accepted");
    ok(strstr(out, "checksum didn't match") != NULL,
       "bad authenticator: warns checksum didn't match");

    if (system("printf 'ClientAuthname: \\r\\nClientPassword: x\\r\\n.\\r\\n' "
               "| ../../authprogs/radius -f /nonexistent >radius.out 2>&1")
        == 0)
        ok(false, "missing config file: fails");
    else
        ok(true, "missing config file: fails");
    unlink("radius.out");

    {
        FILE *f = fopen("radius.conf", "w");
        int s;

        if (f == NULL)
            sysbail("cannot create radius.conf");
        fprintf(f, "server test {\n  radhost: 127.0.0.1\n  secret: x\n}\n");
        fclose(f);
        s = system("printf 'ClientAuthname: \\r\\nClientPassword: x\\r\\n"
                   ".\\r\\n' | ../../authprogs/radius -f radius.conf "
                   ">radius.out 2>&1");
        status = WIFEXITED(s) ? WEXITSTATUS(s) : -1;
        unlink("radius.conf");
        unlink("radius.out");
        ok(status != 0, "empty username: fails");
    }

    status = run_case("frank", "pw", "frank", "pw", "", REPLY_SILENT, out,
                      sizeof(out), &server_ok);
    ok(status != 0, "silent server: fails");
    ok(strstr(out, "timeout talking to remote radius server") != NULL,
       "silent server: reports the timeout");
    ok(server_ok, "silent server: radius tried three times");

    /* The normal case again, with radius's socket above FD_SETSIZE. */
    {
        struct rlimit rl;
        rlim_t need = FD_SETSIZE + 20;

        if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
            sysbail("cannot get RLIMIT_NOFILE");
        if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < need) {
            skip_block(4, "descriptor limit %lu too low",
                       (unsigned long) rl.rlim_max);
            return 0;
        }
        if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < need) {
            rl.rlim_cur = need;
            if (setrlimit(RLIMIT_NOFILE, &rl) < 0)
                sysbail("cannot raise RLIMIT_NOFILE");
        }
    }
    fill_fds = true;
    status = run_case("alice", "secret1", "alice", "secret1", "",
                      REPLY_CHECK, out, sizeof(out), &server_ok);
    fill_fds = false;
    is_int(0, status, "socket above FD_SETSIZE: exit status 0");
    is_string("User:alice\r\n", out,
              "socket above FD_SETSIZE: reports the user");
    fill_fds = true;
    started = time(NULL);
    status = run_case("frank", "pw", "frank", "pw", "", REPLY_SILENT, out,
                      sizeof(out), &server_ok);
    fill_fds = false;
    /* Three tries of 6 s each; the 60 s alarm means radius hung. */
    ok(status != 0 && time(NULL) - started < 40,
       "socket above FD_SETSIZE, silent server: fails in time");
    ok(strstr(out, "timeout talking to remote radius server") != NULL,
       "socket above FD_SETSIZE, silent server: reports the timeout");

    return 0;
}
