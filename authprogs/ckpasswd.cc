/*
**  The default username/password authenticator.
**
**  This program is intended to be run by nnrpd and handle usernames and
**  passwords.  It can authenticate against a regular flat file (the type
**  managed by htpasswd), a DBM file, the system password file or shadow file,
**  or PAM.
**
**  Originally written by Aidan Cully <aidan@panix.com> in 1998.
**
**  Various bug fixes, code and documentation improvements since then
**  in 1998-2003, 2005, 2006, 2013, 2017, 2021, 2022, 2024, 2026.
*/

#include "portable/system.h"

#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/qio.h"
#include "inn/vector.h"

#include "libinnauth.h"

#if HAVE_CRYPT_H
#    include <crypt.h>
#endif

#include <fcntl.h>
#include <optional>
#include <string>
#include <grp.h>
#include <pwd.h>


/*
**  If compiling with Berkeley DB, use its ndbm compatibility layer
**  in preference to other libraries.
*/
#if defined(HAVE_DBM) || defined(HAVE_BDB_NDBM)
#    if HAVE_BDB_NDBM
#        define DB_DBM_HSEARCH 1
#        include <db.h>
#    elif HAVE_NDBM_H
#        include <ndbm.h>
#    elif HAVE_DB1_NDBM_H
#        include <db1/ndbm.h>
#    elif HAVE_GDBM_SLASH_NDBM_H
#        include <gdbm/ndbm.h>
#    elif HAVE_GDBM_HYPHEN_NDBM_H
#        include <gdbm-ndbm.h>
#    endif
#    define OPT_DBM "d:"
#else
#    define OPT_DBM ""
#endif

#if HAVE_GETSPNAM
#    include <shadow.h>
#    define OPT_SHADOW "s"
#else
#    define OPT_SHADOW ""
#endif

#if HAVE_PAM
#    if HAVE_SECURITY_PAM_APPL_H
#        include <security/pam_appl.h>
#    else
#        include <pam/pam_appl.h>
#    endif
#endif


/*
**  The PAM conversation function.
**
**  Since we already have all the information and can't ask the user
**  questions, we can't quite follow the real PAM protocol.  Instead, we just
**  return the password in response to every question that PAM asks.  There
**  appears to be no generic way to determine whether the message in question
**  is indeed asking for the password....
**
**  This function allocates an array of struct pam_response to return to the
**  PAM libraries that's never freed.  For this program, this isn't much of an
**  issue, since it will likely only be called once and then the program will
**  exit.  This function uses malloc and strdup instead of xmalloc and xstrdup
**  intentionally so that the PAM conversation will be closed cleanly if we
**  run out of memory rather than simply terminated.
**
**  appdata_ptr contains the password we were given.
*/
#if HAVE_PAM
static int
pass_conv(int num_msg, PAM_CONST struct pam_message **msgm UNUSED,
          struct pam_response **response, void *appdata_ptr)
{
    int i;

    *response = static_cast<struct pam_response *>(
        malloc(num_msg * sizeof(struct pam_response)));
    if (*response == nullptr)
        return PAM_CONV_ERR;
    for (i = 0; i < num_msg; i++) {
        (*response)[i].resp = strdup((char *) appdata_ptr);
        (*response)[i].resp_retcode = 0;
    }
    return PAM_SUCCESS;
}
#endif /* HAVE_PAM */


/*
**  Authenticate a user via PAM.
**
**  Attempts to authenticate a user with PAM, returning true if the user
**  successfully authenticates and false otherwise.  Note that this function
**  doesn't attempt to handle any remapping of the authenticated user by the
**  PAM stack, but just assumes that the authenticated user was the same as
**  the username given.
**
**  Right now, all failures are handled via die.  This may be worth revisiting
**  in case we want to try other authentication methods if this fails for a
**  reason other than the system not having PAM support.
*/
#if !HAVE_PAM
static bool
auth_pam(const char *username UNUSED, char *password UNUSED)
{
    return false;
}
#else
static bool
auth_pam(const char *username, char *password)
{
    pam_handle_t *pamh;
    struct pam_conv conv;
    int status;

    conv.conv = pass_conv;
    conv.appdata_ptr = password;
    status = pam_start("nnrpd", username, &conv, &pamh);
    if (status != PAM_SUCCESS)
        die("pam_start failed: %s", pam_strerror(pamh, status));
    status = pam_authenticate(pamh, PAM_SILENT);
    if (status != PAM_SUCCESS)
        die("pam_authenticate failed: %s", pam_strerror(pamh, status));
    status = pam_acct_mgmt(pamh, PAM_SILENT);
    if (status != PAM_SUCCESS)
        die("pam_acct_mgmt failed: %s", pam_strerror(pamh, status));
    status = pam_end(pamh, status);
    if (status != PAM_SUCCESS)
        die("pam_end failed: %s", pam_strerror(pamh, status));

    /* If we get to here, the user successfully authenticated. */
    return true;
}
#endif /* HAVE_PAM */


/*
**  Try to get a password out of a dbm file.  The dbm file should have the
**  username for the key and the encrypted password as the value.  The
**  encrypted password, if found, is returned as a std::string;
**  otherwise, std::nullopt is returned.
*/
#if !(defined(HAVE_DBM) || defined(HAVE_BDB_NDBM))
static std::optional<std::string>
password_dbm(char *user UNUSED, const char *file UNUSED)
{
    return std::nullopt;
}
#else
static std::optional<std::string>
password_dbm(char *name, const char *file)
{
    datum key, value;
    DBM *database;

    database = dbm_open(file, O_RDONLY, 0600);
    if (database == nullptr)
        return std::nullopt;
    key.dptr = name;
    key.dsize = strlen(name);
    value = dbm_fetch(database, key);
    if (value.dptr == nullptr) {
        dbm_close(database);
        return std::nullopt;
    }
    std::string password(static_cast<const char *>(value.dptr), value.dsize);
    dbm_close(database);
    return password;
}
#endif /* HAVE_DBM || HAVE_BDB_NDBM */


/*
**  Try to get a password out of the system /etc/shadow file.  The encrypted
**  password, if found, is returned as a std::string; otherwise,
**  nothing (std::nullopt) is returned.
*/
#if !HAVE_GETSPNAM
static std::optional<std::string>
password_shadow(const char *user UNUSED)
{
    return std::nullopt;
}
#else
static std::optional<std::string>
password_shadow(const char *user)
{
    struct spwd *spwd;

    spwd = getspnam(user);
    if (spwd != NULL)
        return std::string(spwd->sp_pwdp);
    return std::nullopt;
}
#endif /* HAVE_GETSPNAM */


/*
**  Try to get a password out of a file.  The encrypted password, if found, is
**  returned as a std::string; otherwise, std::nullopt.
*/
static std::optional<std::string>
password_file(const char *username, const char *file)
{
    QIOSTATE *qp;
    char *line;
    struct cvector *info = nullptr;

    qp = QIOopen(file);
    if (qp == nullptr)
        return std::nullopt;
    for (line = QIOread(qp); line != nullptr; line = QIOread(qp)) {
        if (*line == '#' || *line == '\n')
            continue;
        info = cvector_split(line, ':', info);
        if (info->count < 2 || strcmp(info->strings[0], username) != 0)
            continue;
        std::string password(info->strings[1]);
        QIOclose(qp);
        cvector_free(info);
        return password;
    }
    if (QIOtoolong(qp))
        die("line too long in %s", file);
    if (QIOerror(qp))
        sysdie("error reading %s", file);
    QIOclose(qp);
    cvector_free(info);
    return std::nullopt;
}


/*
**  Try to get a password out of the system password file.  The encrypted
**  password, if found, is returned as a std::string; otherwise,
**  nothing (std::nullopt) is returned.
*/
static std::optional<std::string>
password_system(const char *username)
{
    struct passwd *pwd;

    pwd = getpwnam(username);
    if (pwd != nullptr)
        return std::string(pwd->pw_passwd);
    return std::nullopt;
}


/*
**  Try to get the name of a user's primary group out of the system group
**  file.  The group, if found, is returned; otherwise (or if the username
**  is not found) the result is empty.
*/
static std::string
group_system(const char *username)
{
    struct passwd *pwd;
    struct group *gr;

    pwd = getpwnam(username);
    if (pwd == nullptr)
        return {};
    gr = getgrgid(pwd->pw_gid);
    if (gr == nullptr)
        return {};
    return gr->gr_name;
}


/*
**  Output username (and group, if desired) in correct return format.
*/
static void
output_user(const char *username, bool wantgroup)
{
    if (wantgroup) {
        std::string group = group_system(username);
        if (group.empty())
            die("group info for user %s not available", username);
        printf("User:%s@%s\r\n", username, group.c_str());
    } else
        print_user(username);
}


/*
**  Main routine.
**
**  We handle the variances between systems with #if blocks above, so that
**  this code can look fairly clean.
*/
int
main(int argc, char *argv[])
{
    enum authtype {
        AUTH_NONE,
        AUTH_SHADOW,
        AUTH_FILE,
        AUTH_DBM
    };

    int opt;
    const char *hash;
    enum authtype type = AUTH_NONE;
    bool wantgroup = false;
    const char *filename = nullptr;
    std::optional<auth_info> authinfo;
    bool have_user = false, have_pass = false;
    std::optional<std::string> password;

    message_program_name = "ckpasswd";

    while ((opt = getopt(argc, argv, "gf:u:p:" OPT_DBM OPT_SHADOW)) != -1) {
        switch (opt) {
        case 'g':
            if (type == AUTH_DBM || type == AUTH_FILE)
                die("-g option is incompatible with -d or -f");
            wantgroup = true;
            break;
        case 'd':
            if (type != AUTH_NONE)
                die("only one of -s, -f, or -d allowed");
            if (wantgroup)
                die("-g option is incompatible with -d or -f");
            type = AUTH_DBM;
            filename = optarg;
            break;
        case 'f':
            if (type != AUTH_NONE)
                die("only one of -s, -f, or -d allowed");
            if (wantgroup)
                die("-g option is incompatible with -d or -f");
            type = AUTH_FILE;
            filename = optarg;
            break;
        case 's':
            if (type != AUTH_NONE)
                die("only one of -s, -f, or -d allowed");
            type = AUTH_SHADOW;
            break;
        case 'u':
            if (!authinfo)
                authinfo.emplace();
            authinfo->username = optarg;
            have_user = true;
            break;
        case 'p':
            if (!authinfo)
                authinfo.emplace();
            authinfo->password = optarg;
            have_pass = true;
            break;
        default:
            exit(1);
        }
    }
    if (argc != optind)
        die("extra arguments given");
    if (authinfo && !have_user)
        die("-u option is required if -p option is given");
    if (authinfo && !have_pass)
        die("-p option is required if -u option is given");

    /* Unless a username or password was given on the command line, assume
       we're being run by nnrpd. */
    if (!authinfo)
        authinfo = get_auth_info(stdin);
    if (!authinfo)
        die("no authentication information from nnrpd");
    if (authinfo->username.empty())
        die("null username");

    /* Run the appropriate authentication routines. */
    switch (type) {
    case AUTH_SHADOW:
        password = password_shadow(authinfo->username.c_str());
        if (!password)
            password = password_system(authinfo->username.c_str());
        break;
    case AUTH_FILE:
        password = password_file(authinfo->username.c_str(), filename);
        break;
    case AUTH_DBM:
        password = password_dbm(authinfo->username.data(), filename);
        break;
    case AUTH_NONE:
        /* PAM keeps the password pointer only until auth_pam returns. */
        if (auth_pam(authinfo->username.c_str(), authinfo->password.data())) {
            output_user(authinfo->username.c_str(), wantgroup);
            exit(0);
        }
        password = password_system(authinfo->username.c_str());
        break;
    }

    if (!password)
        die("user %s unknown", authinfo->username.c_str());
    hash = crypt(authinfo->password.c_str(), password->c_str());
    if (hash == nullptr || *password != hash)
        die("invalid password for user %s", authinfo->username.c_str());

    /* The password matched. */
    output_user(authinfo->username.c_str(), wantgroup);
    exit(0);
}
