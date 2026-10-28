#include "portable/system.h"

#include <ctype.h>
#include <errno.h>

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/paths.h"


typedef struct _DDENTRY {
    char *Pattern;
    char *Value;
    int Weight;
} DDENTRY;

struct _DDHANDLE {
    int Count;
    DDENTRY *Entries;
    DDENTRY *Current;
};
using DDHANDLE = struct _DDHANDLE;

struct _DDHANDLE *
DDstart(FILE *FromServer, FILE *ToServer)
{
    DDHANDLE *h;
    DDENTRY *ep;
    FILE *F;
    char buff[BUFSIZ];
    char *p;
    char *q;
    char *path;
    int i;
    int fd;
    char *name = nullptr;

    /* Open the file. */
    path = concatpath(innconf->pathetc, INN_PATH_DISTPATS);
    F = fopen(path, "r");
    free(path);
    if (F == nullptr) {
        /* Not available locally; try remotely. */
        if (FromServer == nullptr || ToServer == nullptr)
            /* We're probably nnrpd running on the server and the
             * file isn't installed.  Oh well. */
            return nullptr;
        name = concatpath(innconf->pathtmp, INN_PATH_TEMPACTIVE);
        fd = mkstemp(name);
        if (fd < 0)
            return nullptr;
        close(fd);
        if ((F = CA_listopen(name, FromServer, ToServer, "DISTRIB.PATS"))
            == nullptr)
            return nullptr;
    }

    /* Count lines. */
    for (i = 0; fgets(buff, sizeof buff, F) != nullptr; i++)
        continue;

    /* Allocate space for the handle. */
    if ((h = xmalloc(sizeof(DDHANDLE))) == nullptr) {
        i = errno;
        fclose(F);
        if (name != nullptr)
            unlink(name);
        errno = i;
        return nullptr;
    }
    h->Count = 0;
    h->Current = nullptr;
    if (i == 0) {
        return nullptr;
    } else if ((h->Entries = xmalloc(sizeof(DDENTRY) * i)) == nullptr) {
        i = errno;
        free(h);
        fclose(F);
        if (name != nullptr)
            unlink(name);
        errno = i;
        return nullptr;
    }

    fseeko(F, 0, SEEK_SET);
    for (ep = h->Entries; fgets(buff, sizeof buff, F) != nullptr;) {
        if ((p = strchr(buff, '\n')) != nullptr)
            *p = '\0';
        if (buff[0] == '\0' || buff[0] == '#')
            continue;
        if ((p = strchr(buff, ':')) == nullptr
            || (q = strchr(p + 1, ':')) == nullptr)
            continue;
        *p++ = '\0';
        ep->Weight = atoi(buff);
        ep->Pattern = xstrdup(p);
        q = strchr(ep->Pattern, ':');
        *q++ = '\0';
        ep->Value = q;
        ep++;
    }
    h->Count = ep - h->Entries;

    fclose(F);
    if (name != nullptr)
        unlink(name);
    return h;
}


void
DDcheck(DDHANDLE *h, char *group)
{
    DDENTRY *ep;
    int i;
    int w;

    if (h == nullptr || group == nullptr)
        return;

    w = h->Current ? h->Current->Weight : -1;
    for (ep = h->Entries, i = h->Count; --i >= 0; ep++)
        if (ep->Weight > w && uwildmat(group, ep->Pattern)) {
            h->Current = ep;
            w = ep->Weight;
        }
}


char *
DDend(DDHANDLE *h)
{
    static char NIL[] = "";
    char *p;
    int i;
    DDENTRY *ep;

    if (h == nullptr) {
        p = NIL;
        return xstrdup(p);
    }

    if (h->Current == nullptr)
        p = NIL;
    else
        p = h->Current->Value;
    p = xstrdup(p);

    for (ep = h->Entries, i = h->Count; --i >= 0; ep++)
        free(ep->Pattern);
    free(h->Entries);
    free(h);
    return p;
}

#if defined(TEST)
int
main(int ac, char *av[])
{
    struct _DDHANDLE *h;
    char *p;
    FILE *FromServer;
    FILE *ToServer;
    char buff[SMBUF];

    if (NNTPremoteopen(NNTP_PORT, &FromServer, &ToServer, buff, sizeof(buff))
        < 0) {
        if ((p = strchr(buff, '\n')) != NULL)
            *p = '\0';
        if ((p = strchr(buff, '\r')) != NULL)
            *p = '\0';
        if (buff[0])
            fprintf(stderr, "%s\n", buff);
        else
            perror("Can't connect");
        exit(1);
    }

    if ((h = DDstart(FromServer, ToServer)) == NULL)
        perror("Init failed, proceeding anyway");
    while ((p = *++av) != NULL)
        DDcheck(h, p);
    p = DDend(h);
    printf(">%s<\n", p);
    exit(0);
    /* NOTREACHED */
}
#endif /* defined(TEST) */
