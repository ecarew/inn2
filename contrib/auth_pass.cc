/*
 *    auth_pass.c
 *
 * Abstract:
 *
 *    This module is the complete source for a sample "authinfo generic"
 *    program.  This program takes a user's login name and password
 *    (supplied either as arguments or as responses to prompts) and
 *    validates them against the contents of the password database.
 *
 *    If the user properly authenticates themselves, an nnrp.auth style
 *    record indicating the user's authenticated login and permitting
 *    reading and posting to all groups is output on stderr (for reading by
 *    nnrpd) and the program exits with a 0 status.  If the user fails to
 *    authenticate, then a record with the attempted login name and no
 *    access is output on stderr and a non-zero exit status is returned.
 *
 * Exit statuses:
 *    0    Successfully authenticated.
 *    1    getpeername() failed, or returned a bad address family.
 *    2    Entry not found in password file.
 *    3    No permission to read passwords, or the password field is not a
 *         password hash ('*', 'x', empty, or locked with '!').
 *    4    Bad password match.
 *    5    Cannot read the username or password, or one is too long.
 *
 * Environment:
 *    Run by nnrpd with stdin/stdout connected to the reader and stderr
 *    connected back to nnrpd.  This program will need to be run as suid
 *    root on systems where passwords are stored in a file readable only by
 *    root.
 *
 * Written 1996 July 6 by Douglas Wade Needham (dneedham@oucsace.cs.ohiou.edu).
 *
 * Various bug fixes, code and documentation improvements since then
 * in 1997, 2002, 2003, 2007, 2011, 2014, 2021, 2024, 2026.
 */

#include "portable/system.h"

#include "portable/socket.h"
#include <errno.h>
#include <netdb.h>
#include <pwd.h>

#if HAVE_CRYPT_H
#    include <crypt.h>
#endif

#include <string>

/* Longest username or password accepted (not counting the line ending).  A
   longer one is refused, never cut short: cutting a name short could log in
   a different user. */
static const size_t MAX_FIELD = 255;

/* Read a line from stdin, without the line ending.  Returns false on end of
   file, error, or a line that does not fit. */
static bool
read_field(std::string &out)
{
    char buf[MAX_FIELD + 2];

    if (fgets(buf, sizeof(buf), stdin) == NULL)
        return false;
    out = buf;
    if (out.empty() || out.back() != '\n')
        return false; /* too long, or the input ended mid-line */
    return true;
}

/* Remove carriage returns and newlines from the end. */
static void
strip_line_end(std::string &s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
}

/* Equal, taking the same time wherever the strings differ. */
static bool
same_string(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    unsigned char diff = la != lb;

    for (size_t i = 0; i < la; i++)
        diff |= static_cast<unsigned char>(a[i] ^ b[i % (lb ? lb : 1)]);
    return diff == 0;
}

int
main(int argc, char **argv)
{
    std::string username;
    std::string password;

    /*
     * Get the user name and password if needed.
     */
    if (argc < 2) {
        fprintf(stdout, "Username: ");
        fflush(stdout);
        if (!read_field(username)) {
            fprintf(stderr, "cannot read username\n");
            exit(5);
        }
    } else {
        username = argv[1];
    }
    if (argc < 3) {
        fprintf(stdout, "Password: ");
        fflush(stdout);
        if (!read_field(password)) {
            fprintf(stderr, "cannot read password\n");
            exit(5);
        }
    } else {
        password = argv[2];
    }
    strip_line_end(username);
    strip_line_end(password);
    if (username.size() > MAX_FIELD || password.size() > MAX_FIELD) {
        fprintf(stderr, "username or password too long\n");
        exit(5);
    }

    /*
     *  Get the hostname of the peer.
     */
    std::string peername;
    struct sockaddr_storage ss;
    socklen_t length = sizeof(ss);
    if (getpeername(0, reinterpret_cast<struct sockaddr *>(&ss), &length)
        < 0) {
        if (!isatty(0)) {
            fprintf(stderr, "cant getpeername()::%s:+:!*\n", username.c_str());
            exit(1);
        }
        peername = "localhost";
    } else if (ss.ss_family != AF_INET && ss.ss_family != AF_INET6) {
        fprintf(stderr, "Bad address family %ld::%s:+:!*\n",
                (long) ss.ss_family, username.c_str());
        exit(1);
    } else {
        char host[NI_MAXHOST];

        /* The name if it can be found, else the address. */
        if (getnameinfo(reinterpret_cast<struct sockaddr *>(&ss), length, host,
                        sizeof(host), NULL, 0, NI_NAMEREQD)
                != 0
            && getnameinfo(reinterpret_cast<struct sockaddr *>(&ss), length,
                           host, sizeof(host), NULL, 0, NI_NUMERICHOST)
                   != 0)
            strlcpy(host, "unknown", sizeof(host));
        peername = host;
    }

    /*
     *  Get the user name in the passwd file.
     */
    struct passwd *pwd = getpwnam(username.c_str());
    if (pwd == NULL) {

        /*
         *  No entry in the passwd file.
         */
        fprintf(stderr, "%s::%s:+:!*\n", peername.c_str(), username.c_str());
        exit(2);
    }

    /*
     *  Make sure we managed to read in a password hash.  "*" means no
     *  permission to read passwords; "x" says the hash is in a shadow file;
     *  empty or "!..." is an account without a usable password.
     */
    const char *hash = pwd->pw_passwd;
    if (hash == NULL || hash[0] == '\0' || hash[0] == '*' || hash[0] == '!'
        || strcmp(hash, "x") == 0) {
        fprintf(stderr, "%s::%s:+:!*\n", peername.c_str(), username.c_str());
        exit(3);
    }

    /*
     *  Verify the password.
     */
    const char *crypted = crypt(password.c_str(), hash);
    if (crypted == NULL || !same_string(hash, crypted)) {

        /*
         * Password was invalid.
         */
        fprintf(stderr, "%s::%s:+:!*\n", peername.c_str(), username.c_str());
        exit(4);
    }

    /*
     *  We managed to authenticate the user.
     */
    fprintf(stderr, "%s:RP:%s:+:*\n", peername.c_str(), username.c_str());
    exit(0);
}
