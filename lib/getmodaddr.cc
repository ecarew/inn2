#include "portable/system.h"

#include <errno.h>

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/nntp.h"
#include "inn/paths.h"


static char *GMApathname = nullptr;
static FILE *GMAfp = nullptr;


/*
**  Close the file opened by GMAlistopen.
*/
static void
GMAclose()
{
    if (GMAfp) {
        fclose(GMAfp);
        GMAfp = nullptr;
    }
    if (GMApathname != nullptr) {
        unlink(GMApathname);
        free(GMApathname);
        GMApathname = nullptr;
    }
}

/*
**  Internal library routine.
*/
static FILE *
GMA_listopen(int fd, FILE *FromServer, FILE *ToServer, const char *request)
{
    char buff[BUFSIZ];
    char expectedanswer[BUFSIZ];
    char *p;
    int oerrno;
    FILE *F;

    F = fdopen(fd, "r+");
    if (F == nullptr)
        return nullptr;

    /* Send a LIST command to and capture the output. */
    if (request == nullptr)
        fprintf(ToServer, "LIST MODERATORS\r\n");
    else
        fprintf(ToServer, "LIST %s\r\n", request);
    fflush(ToServer);

    snprintf(expectedanswer, sizeof(expectedanswer), "%d", NNTP_OK_LIST);

    /* Get the server's reply to our command. */
    if (fgets(buff, sizeof buff, FromServer) == nullptr
        || strncmp(buff, expectedanswer, strlen(expectedanswer)) != 0) {
        oerrno = errno;
        fclose(F);
        GMAclose();
        errno = oerrno;
        return nullptr;
    }

    /* Slurp up the rest of the response. */
    while (fgets(buff, sizeof buff, FromServer) != nullptr) {
        if ((p = strchr(buff, '\r')) != nullptr)
            *p = '\0';
        if ((p = strchr(buff, '\n')) != nullptr)
            *p = '\0';
        if (buff[0] == '.' && buff[1] == '\0') {
            if (ferror(F) || fflush(F) == EOF || fseeko(F, 0, SEEK_SET) != 0)
                break;
            return F;
        }
        fprintf(F, "%s\n", buff);
    }

    /* Ran out of input before finding the terminator; quit. */
    oerrno = errno;
    fclose(F);
    GMAclose();
    errno = oerrno;
    return nullptr;
}

/*
**  Check if the argument is a valid submission template according to RFC 6048.
**  At least, make sure it does not contain "%" except as part of "%s" or "%%",
**  and that only one occurrence of "%s" exists, if any.
*/
static bool
IsValidSubmissionTemplate(const char *string)
{
    bool found = false;
    const char *p;

    /* Not NULL. */
    if (string == nullptr)
        return false;

    p = string;

    while ((p = strchr(p, '%')) != nullptr) {
        /* Look at the next character. */
        p++;

        /* Skip "%%". */
        if (*p == '%') {
            p++;
            continue;
        }

        /* Invalid template if not "%s". */
        if (*p != 's')
            return false;

        /* Invalid template if another "%s". */
        if (found)
            return false;

        found = true;
    }

    return true;
}


/*
**  Read the moderators file, looking for a moderator.
*/
char *
GetModeratorAddress(FILE *FromServer, FILE *ToServer, char *group,
                    char *moderatormailer)
{
    static char address[SMBUF];
    char *p;
    char *save;
    char *path;
    char buff[BUFSIZ];
    char name[SMBUF];
    int fd;

    strlcpy(name, group, sizeof(name));
    address[0] = '\0';

    if (FromServer == nullptr || ToServer == nullptr) {

        /*
         *  This should be part of nnrpd or the like running on the server.
         *  Open the server copy of the moderators file.
         */
        path = concatpath(innconf->pathetc, INN_PATH_MODERATORS);
        GMAfp = fopen(path, "r");
        free(path);
    } else {
        /*
         *  Get a local copy of the moderators file from the server.
         */
        GMApathname = concatpath(innconf->pathtmp, INN_PATH_TEMPMODERATORS);
        fd = mkstemp(GMApathname);
        if (fd >= 0)
            GMAfp = GMA_listopen(fd, FromServer, ToServer, "MODERATORS");
        else
            GMAfp = nullptr;

        /* Fallback to the local copy if the server doesn't have it */
        if (GMAfp == nullptr) {
            path = concatpath(innconf->pathetc, INN_PATH_MODERATORS);
            GMAfp = fopen(path, "r");
            free(path);
        }
    }

    if (GMAfp != nullptr) {
        while (fgets(buff, sizeof buff, GMAfp) != nullptr) {
            /* Skip blank and comment lines. */
            if ((p = strchr(buff, '\n')) != nullptr)
                *p = '\0';
            if (buff[0] == '\0' || buff[0] == '#')
                continue;

            /* Snip off the first word. */
            if ((p = strchr(buff, ':')) == nullptr)
                /* Malformed line... */
                continue;
            *p++ = '\0';

            /* If it pattern-matches the newsgroup, the second field is a
             * format for mailing, with periods changed to dashes. */
            if (uwildmat(name, buff)) {
                for (save = p; ISWHITE(*save); save++)
                    continue;
                for (p = name; *p; p++)
                    if (*p == '.')
                        *p = '-';
                if (IsValidSubmissionTemplate(save)) {
#if __GNUC__ > 4 || defined(__llvm__) || defined(__clang__)
#    pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
                    snprintf(address, sizeof(address), save, name);
#if __GNUC__ > 4 || defined(__llvm__) || defined(__clang__)
#    pragma GCC diagnostic warning "-Wformat-nonliteral"
#endif
                    break;
                }
            }
        }

        GMAclose();
        if (address[0])
            return address;
    }

    /* If we don't have an address, see if the config file has a default. */
    if ((save = moderatormailer) == nullptr)
        return nullptr;

    for (p = name; *p; p++)
        if (*p == '.')
            *p = '-';

    if (IsValidSubmissionTemplate(save)) {
#if __GNUC__ > 4 || defined(__llvm__) || defined(__clang__)
#    pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
        snprintf(address, sizeof(address), save, name);
#if __GNUC__ > 4 || defined(__llvm__) || defined(__clang__)
#    pragma GCC diagnostic warning "-Wformat-nonliteral"
#endif
    } else {
        return nullptr;
    }

    return address;
}
