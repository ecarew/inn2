/*
**  Some utility functions for writing authenticators and resolvers.
*/

#ifndef LIBINNAUTH_H
#define LIBINNAUTH_H

#include "config.h"

#include "portable/socket.h"
#include <stdio.h>

#include <optional>
#include <string>


/* Holds the resolver information from nnrpd. */
struct res_info {
    std::string clienthostname;
    std::string clientip;
    std::string clientport;
    std::string localip;
    std::string localport;
};

/* Holds the authentication information from nnrpd. */
struct auth_info {
    std::string username;
    std::string password;
};

/* Reads connection information from a stream (normally stdin, when talking
   to nnrpd) and returns it, or nothing on failure.  Every field must have
   been sent (though it may be empty); if one is missing, that is logged and
   nothing is returned. */
std::optional<auth_info> get_auth_info(FILE *);
std::optional<res_info> get_res_info(FILE *);

BEGIN_DECLS

/* Return the user string to nnrpd. */
extern void print_user(const char *);

END_DECLS

#endif /* !LIBINNAUTH_H */
