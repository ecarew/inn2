/*
**  Common code for authenticators and resolvers.
**
**  Collects common code to read information from nnrpd that should be done
**  the same for all authenticators, and common code to get information about
**  the incoming connection.
*/

#include "portable/system.h"

#include "portable/socket.h"

#include "inn/libinn.h"
#include "inn/messages.h"
#include "libinnauth.h"

#define NAMESTR "ClientAuthname: "
#define PASSSTR "ClientPassword: "

#define CLIHOST "ClientHost: "
#define CLIIP   "ClientIP: "
#define CLIPORT "ClientPort: "
#define LOCIP   "LocalIP: "
#define LOCPORT "LocalPort: "

/*
**  Main loop.  If res != NULL, expects to get resolver info from nnrpd, and
**  writes it into the struct.  If auth != NULL, expects to get authentication
**  info from nnrpd, and writes it into the struct.  A field may be sent
**  empty; one not sent at all is an error.
*/
static bool
get_connection_info(FILE *stream, struct res_info *res, struct auth_info *auth)
{
    char buff[SMBUF];
    size_t length;
    /* Which fields were sent, in the order NAMESTR, PASSSTR, CLIHOST,
       CLIIP, CLIPORT, LOCIP, LOCPORT. */
    bool sent[7] = {};
    auto take = [&](int which, const char *prefix, std::string &field) {
        size_t n = strlen(prefix);
        if (strncmp(buff, prefix, n) != 0)
            return false;
        field = buff + n;
        sent[which] = true;
        return true;
    };

    /* Read input from nnrpd a line at a time, stripping \r\n. */
    while (fgets(buff, sizeof(buff), stream) != nullptr) {
        length = strlen(buff);
        if (length == 0 || buff[length - 1] != '\n')
            return false;
        buff[length - 1] = '\0';
        if (length > 1 && buff[length - 2] == '\r')
            buff[length - 2] = '\0';

        /* Parse */
        if (strncmp(buff, ".", 2) == 0)
            break;
        else if (auth != nullptr && take(0, NAMESTR, auth->username))
            continue;
        else if (auth != nullptr && take(1, PASSSTR, auth->password))
            continue;
        else if (res != nullptr && take(2, CLIHOST, res->clienthostname))
            continue;
        else if (res != nullptr && take(3, CLIIP, res->clientip))
            continue;
        else if (res != nullptr && take(4, CLIPORT, res->clientport))
            continue;
        else if (res != nullptr && take(5, LOCIP, res->localip))
            continue;
        else if (res != nullptr && take(6, LOCPORT, res->localport))
            continue;
        else {
            debug("libinnauth: unexpected data from nnrpd: \"%s\"", buff);
        }
    }

    /* If some field is missing, error out. */
    if (auth != nullptr && !(sent[0] && sent[1])) {
        warn("libinnauth: requested authenticator data not sent by nnrpd");
        return false;
    }
    if (res != nullptr
        && !(sent[2] && sent[3] && sent[4] && sent[5] && sent[6])) {
        warn("libinnauth: requested resolver data not sent by nnrpd");
        return false;
    }
    return true;
}


/*
**  Read resolver information from nnrpd.
*/
std::optional<res_info>
get_res_info(FILE *stream)
{
    res_info res;

    if (get_connection_info(stream, &res, nullptr))
        return res;
    return std::nullopt;
}


/*
**  Read authenticator information from nnrpd.
*/
std::optional<auth_info>
get_auth_info(FILE *stream)
{
    auth_info auth;

    if (get_connection_info(stream, nullptr, &auth))
        return auth;
    return std::nullopt;
}


/*
**  Print the User: result on standard output in the format expected by
**  nnrpd.  The string passed in should be exactly the user, with no
**  extraneous leading or trailing whitespace.
*/
void
print_user(const char *user)
{
    printf("User:%s\r\n", user);
}
