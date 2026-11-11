/*
**  Domain authenticator.
**
**  Compares the domain of the client connection to the first argument given
**  on the command line, and returns the host portion of the connecting host
**  as the user if it matches.
*/

#include "portable/system.h"

#include "inn/libinn.h"
#include "inn/messages.h"
#include "libinnauth.h"

int
main(int argc, char *argv[])
{
    std::optional<res_info> res;

    if (argc != 2)
        die("Usage: domain <domain>");
    message_program_name = "domain";

    /* Read the connection information from stdin. */
    res = get_res_info(stdin);
    if (!res)
        die("did not get ClientHost data from nnrpd");
    const std::string &host = res->clienthostname;
    const char *domain = argv[1];

    /* Check the host against the provided domain.  Allow the domain to be
       specified both with and without a leading period; if without, make sure
       that there is a period right before where it matches in the host. */
    size_t at = host.find(domain);
    if (at == 0)
        die("host %s matches the domain exactly", host.c_str());
    if (at == std::string::npos
        || (domain[0] != '.' && at != 0 && host[at - 1] != '.'))
        die("host %s didn't match domain %s", host.c_str(), domain);

    /* Peel off the portion of the host before where the provided domain
       matches and return it as the user. */
    if (domain[0] != '.')
        at--;
    print_user(host.substr(0, at).c_str());
    return 0;
}
