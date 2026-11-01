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
**  info from nnrpd, and writes it into the struct.
*/
static bool
get_connection_info(FILE *stream, struct res_info *res, struct auth_info *auth)
{
    char buff[SMBUF];
    size_t length;

    /* Zero fields first (anything remaining NULL after is missing data). */
    if (res != nullptr) {
        res->clienthostname = nullptr;
        res->clientip = nullptr;
        res->clientport = nullptr;
        res->localip = nullptr;
        res->localport = nullptr;
    }
    if (auth != nullptr) {
        auth->username = nullptr;
        auth->password = nullptr;
    }

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
        else if (auth != nullptr
                 && strncmp(buff, NAMESTR, strlen(NAMESTR)) == 0)
            auth->username = xstrdup(buff + strlen(NAMESTR));
        else if (auth != nullptr
                 && strncmp(buff, PASSSTR, strlen(PASSSTR)) == 0)
            auth->password = xstrdup(buff + strlen(PASSSTR));
        else if (res != nullptr
                 && strncmp(buff, CLIHOST, strlen(CLIHOST)) == 0)
            res->clienthostname = xstrdup(buff + strlen(CLIHOST));
        else if (res != nullptr && strncmp(buff, CLIIP, strlen(CLIIP)) == 0)
            res->clientip = xstrdup(buff + strlen(CLIIP));
        else if (res != nullptr
                 && strncmp(buff, CLIPORT, strlen(CLIPORT)) == 0)
            res->clientport = xstrdup(buff + strlen(CLIPORT));
        else if (res != nullptr && strncmp(buff, LOCIP, strlen(LOCIP)) == 0)
            res->localip = xstrdup(buff + strlen(LOCIP));
        else if (res != nullptr
                 && strncmp(buff, LOCPORT, strlen(LOCPORT)) == 0)
            res->localport = xstrdup(buff + strlen(LOCPORT));
        else {
            debug("libinnauth: unexpected data from nnrpd: \"%s\"", buff);
        }
    }

    /* If some field is missing, free the rest and error out. */
    if (auth != nullptr
        && (auth->username == nullptr || auth->password == nullptr)) {
        warn("libinnauth: requested authenticator data not sent by nnrpd");
        return false;
    }
    if (res != nullptr
        && (res->clienthostname == nullptr || res->clientip == nullptr
            || res->clientport == nullptr || res->localip == nullptr
            || res->localport == nullptr)) {
        warn("libinnauth: requested resolver data not sent by nnrpd");
        return false;
    }
    return true;
}


/*
**  Free a struct res_info, including all of its members.
*/
void
free_res_info(struct res_info *res)
{
    if (res == nullptr)
        return;
    if (res->clientip != nullptr)
        free(res->clientip);
    if (res->clientport != nullptr)
        free(res->clientport);
    if (res->localip != nullptr)
        free(res->localip);
    if (res->localport != nullptr)
        free(res->localport);
    if (res->clienthostname != nullptr)
        free(res->clienthostname);
    free(res);
}


/*
**  Free a struct auth_info, including all of its members.
*/
void
free_auth_info(struct auth_info *auth)
{
    if (auth == nullptr)
        return;
    if (auth->username != nullptr)
        free(auth->username);
    if (auth->password != nullptr)
        free(auth->password);
    free(auth);
}


/*
**  Read resolver information from nnrpd, returning an allocated struct on
**  success.
*/
struct res_info *
get_res_info(FILE *stream)
{
    struct res_info *res = xmalloc(sizeof(struct res_info));

    if (get_connection_info(stream, res, nullptr))
        return res;
    free_res_info(res);
    return nullptr;
}


/*
**  Read authenticator information from nnrpd, returning an allocated struct
**  on success.
*/
struct auth_info *
get_auth_info(FILE *stream)
{
    struct auth_info *auth = xmalloc(sizeof(struct auth_info));

    if (get_connection_info(stream, nullptr, auth))
        return auth;
    free_auth_info(auth);
    return nullptr;
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
