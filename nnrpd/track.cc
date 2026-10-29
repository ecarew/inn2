/*
**  User and post tracking database.
*/

#include "portable/system.h"

#include "inn/innconf.h"
#include "nnrpd.h"

#define MAX_LEN 180

/*
**  TrackClient determines whether or not
**  we are interested in tracking the activities
**  of the currently connected host.  We have to
**  rely on an external process to set up the
**  entries in the database though, which makes
**  this only as reliable as the process that
**  sets this up...
*/

/*
**  Format of the input line is the following one.
**    <host>:<username>
*/

int
TrackClient(char *client, char *user, size_t len)
{
    int RARTon;
    FILE *fd;
    char line[MAX_LEN], *p, *pp, *lp;
    char *dbfile;

    dbfile = concatpath(innconf->pathetc, "nnrpd.track");

    RARTon = false;
    strlcpy(user, "unknown", len);

    if ((fd = fopen(dbfile, "r")) != nullptr) {
        while ((fgets(line, (MAX_LEN - 1), fd)) != nullptr) {
            if (line[0] == '#' || line[0] == '\n')
                continue;
            if ((p = strchr(line, ' ')) != nullptr)
                *p = '\0';
            if ((p = strchr(line, '\n')) != nullptr)
                *p = '\0';
            if ((p = strchr(line, ':')) != nullptr) {
                *p++ = '\0';
            } else {
                p = nullptr;
            }
            pp = line;
            if ((lp = strchr(pp, '*')) != nullptr) {
                pp = ++lp;
            }
            if (strstr(client, pp) != nullptr) {
                RARTon = true;
                if (p != nullptr)
                    strlcpy(user, p, len);
                break;
            }
        }
        fclose(fd);
    } else {
        RARTon = false;
        syslog(L_NOTICE, "%s No logging -- can't read %s", Client.host,
               dbfile);
    }

    free(dbfile);
    return RARTon;
}
