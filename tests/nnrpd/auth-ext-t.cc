/*  Test suite for auth_external.
**
**  Written by Russ Allbery in 2005.
**
**  Various bug fixes, code and documentation improvements since then
**  in 2005, 2014, 2018, 2020, 2021, 2026.
*/

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include <fcntl.h>
#include <sys/resource.h>
#include <time.h>

#include "inn/messages.h"
#include "tap/basic.h"
#include "tap/messages.h"
#include "tap/string.h"

#include "../../nnrpd/nnrpd.h"

/* Need this because nnrpd is still bad about global variables. */
double IDLEtime = 0;

/* The basic information that we always send to the program. */
static const char info[] = "\
ClientHost: example.com\r\n\
ClientIP: 10.10.10.10\r\n\
ClientPort: 50000\r\n\
LocalIP: 10.0.0.2\r\n\
LocalPort: 119\r\n";

/* The additional information sent to authenticators. */
static const char userinfo[] = "\
ClientAuthname: tester\r\n\
ClientPassword: s0pers3cret\r\n";

/* Allocate a new client struct and fill it out with our test data. */
static struct client *
client_new(void)
{
    struct client *client;

    client = xmalloc(sizeof(struct client));
    strlcpy(client->host, "example.com", sizeof(client->host));
    strlcpy(client->ip, "10.10.10.10", sizeof(client->ip));
    strlcpy(client->serverip, "10.0.0.2", sizeof(client->serverip));
    client->port = 50000;
    client->serverport = 119;
    return client;
}

/* Validate the input file against the expected data.  Takes a flag indicating
   whether we ran an authenticator. */
static void
validate_input(bool auth)
{
    char *wanted, *seen;

    if (auth)
        wanted = concat(info, userinfo, ".\r\n", (char *) 0);
    else
        wanted = concat(info, ".\r\n", (char *) 0);
    seen = ReadInFile("input", nullptr);
    if (seen == nullptr) {
        syswarn("unable to read input");
        ok(false, "no input");
    } else {
        is_string(wanted, seen, "input is as expected");
        free(seen);
    }
    unlink("input");
    free(wanted);
}

/* Run the test authenticator, checking its input and output.  Takes the fake
   client struct, the argument to pass to the authenticator, the expected
   username, and the expected error output.  Tries it both as a resolver and
   an authenticator to be sure there are no surprises.  Returns the next test
   number. */
static void
test_external(struct client *client, const char *arg, const char *user,
              const char *error)
{
    char *auth_test_path, *result, *command;

    diag("mode %s", arg);

    auth_test_path = test_file_path("nnrpd/auth-test");
    if (auth_test_path == nullptr)
        bail("cannot find nnrpd/auth-test helper");

    basprintf(&command, "%s %s", auth_test_path, arg);
    errors_capture();
    result = auth_external(client, command, ".", nullptr, nullptr);
    errors_uncapture();
    validate_input(false);
    is_string(user, result, "user");
    is_string(error, errors, "errors");
    if (errors && (error == nullptr || strcmp(error, errors) != 0))
        warn("%s", errors);
    free(errors);
    errors = nullptr;

    errors_capture();
    result = auth_external(client, command, ".", "tester", "s0pers3cret");
    errors_uncapture();
    validate_input(true);
    is_string(user, result, "user with username and password");
    is_string(error, errors, "errors with username and password");
    if (errors && (error == nullptr || strcmp(error, errors) != 0))
        warn("%s", errors);
    free(errors);
    errors = nullptr;

    test_file_path_free(auth_test_path);
}

/* Fills every free descriptor up to FD_SETSIZE + 10, so the pipes
   auth_external makes land above FD_SETSIZE, where a select()-based wait
   fails.  Returns the first descriptor filled, or -1 if the descriptor
   limit is too low; fill_end closes them again. */
static int
fill_start(void)
{
    struct rlimit rl;
    rlim_t need = FD_SETSIZE + 20;
    int first, i;

    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
        sysbail("cannot get RLIMIT_NOFILE");
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < need)
        return -1;
    if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < need) {
        rl.rlim_cur = need;
        if (setrlimit(RLIMIT_NOFILE, &rl) < 0)
            sysbail("cannot raise RLIMIT_NOFILE");
    }
    first = open("/dev/null", O_RDONLY);
    if (first < 0)
        sysbail("cannot open /dev/null");
    for (i = first + 1; i <= FD_SETSIZE + 10; i++)
        if (fcntl(i, F_GETFD) < 0 && dup2(first, i) < 0)
            sysbail("cannot fill descriptor %d", i);
    return first;
}

static void
fill_end(int first)
{
    for (int i = first; i <= FD_SETSIZE + 10; i++)
        close(i);
}

/* A program that never answers: auth_external gives up after five
   seconds, kills it, and reports the timeout.  Three checks. */
static void
test_hang(struct client *client, const char *what)
{
    char *auth_test_path, *command, *result;
    time_t start;

    auth_test_path = test_file_path("nnrpd/auth-test");
    if (auth_test_path == nullptr)
        bail("cannot find nnrpd/auth-test helper");
    basprintf(&command, "%s hang", auth_test_path);
    start = time(nullptr);
    errors_capture();
    result = auth_external(client, command, ".", nullptr, nullptr);
    errors_uncapture();
    unlink("input");
    is_string(nullptr, result, "%s: silent program returns no user", what);
    ok(errors != nullptr && strstr(errors, "program timeout") != nullptr,
       "%s: silent program reports the timeout", what);
    ok(time(nullptr) - start < 15, "%s: after the timeout, not later",
       what);
    free(errors);
    errors = nullptr;
    free(command);
    test_file_path_free(auth_test_path);
}

int
main(void)
{
    struct client *client;
    char *result;
    int first;

    plan(14 * 6 + 2 + 2 * 3);

    client = client_new();

    test_external(client, "okay", "tester", nullptr);
    test_external(client, "garbage", "tester", nullptr);
    test_external(client, "error", nullptr,
                  "example.com auth: program error: This is an error\n");
    test_external(client, "interspersed", "tester",
                  "example.com auth: program error: This is an error\n");
    test_external(client, "empty", nullptr, nullptr);
    test_external(client, "empty-error", nullptr,
                  "example.com auth: program exited with status 1\n");
    test_external(client, "okay-error", nullptr,
                  "example.com auth: program exited with status 1\n");
    test_external(client, "signal", nullptr,
                  "example.com auth: program caught signal 1\n");
    test_external(client, "newline", "tester", nullptr);
    test_external(client, "partial", "tester", nullptr);
    test_external(client, "partial-close", "tester", nullptr);
    test_external(client, "partial-error", nullptr,
                  "example.com auth: program error: This is an error\n");

    errors_capture();
    result = auth_external(client, "", ".", nullptr, nullptr);
    errors_uncapture();
    is_string(nullptr, result, "empty external command returns no user");
    is_string("example.com auth: empty external command\n", errors,
              "empty external command is reported");
    free(errors);
    errors = nullptr;

    test_hang(client, "low descriptors");

    /* Output and error pipes above FD_SETSIZE. */
    first = fill_start();
    if (first < 0)
        skip_block(2 * 6 + 3, "descriptor limit too low");
    else {
        test_external(client, "okay", "tester", nullptr);
        test_external(client, "interspersed", "tester",
                      "example.com auth: program error: This is an error\n");
        test_hang(client, "descriptors above FD_SETSIZE");
        fill_end(first);
    }

    free(client);
    return 0;
}
