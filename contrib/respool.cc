/*
** Refile articles into the storage manager under the current storage.conf
** rules, deleting articles from their old place in the spool.
** Written 10-09-99 by rmtodd@servalan.servalan.com
**
** Various bug fixes, code and documentation improvements since then
** in 2002-2004, 2006, 2011, 2014, 2015, 2021, 2024, 2026.
**
** The article is stored in its new place before the old copy is removed, so
** an error never loses it.
**
** Note that history and overview will have to be rebuilt for the moved
** articles to be visible after they're moved.
*/

/* include foo needed by libinn/storage manager */
#include "portable/system.h"

#include <errno.h>

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/paths.h"
#include "inn/qio.h"
#include "inn/storage.h"
#include "inn/wire.h"

#include <sys/uio.h>

#include <string>

static const char *ME;

/* Refile the article a line names.  Returns false on an error that should
   make the exit status non-zero; a token that is gone is only skipped. */
static bool
ProcessLine(char *line)
{
    std::string text_token = line;

    /* zap newline at end of the token, if present. */
    while (!text_token.empty()
           && (text_token.back() == '\n' || text_token.back() == '\r'))
        text_token.pop_back();
    if (text_token.empty())
        return true;

    if (!IsToken(text_token.c_str())) {
        fprintf(stderr, "%s: bad token format %s\n", ME, text_token.c_str());
        return false;
    }
    TOKEN token = TextToToken(text_token.c_str());

    ARTHANDLE *art = SMretrieve(token, RETR_ALL);
    if (art == NULL)
        return true;
    std::string data(art->data, art->len);
    SMfreearticle(art);

    /* Describe the article to the storage manager the way sm -s does: the
     * text, its Expires time and the groups it goes to (the Newsgroups or,
     * with storeonxref, Xref header field body).  Without the groups the
     * storage manager cannot choose where to put it. */
    ARTHANDLE newart = ARTHANDLE_INITIALIZER;
    struct iovec iov;
    const char *text = data.data();
    size_t size = data.size();
    const char *start, *end;

    iov.iov_base = const_cast<char *>(text);
    iov.iov_len = size;
    newart.data = text;
    newart.len = size;
    newart.iov = &iov;
    newart.iovcnt = 1;
    newart.arrived = (time_t) 0; /* set current time */
    newart.expires = 0;
    newart.token = (TOKEN *) NULL;

    start = wire_findheader(text, size, "Expires", true);
    if (start != NULL && (end = wire_endheader(start, text + size - 1))) {
        std::string expires(start, end - start);
        time_t when = parsedate_rfc5322_lax(expires.c_str());
        newart.expires = when == (time_t) -1 ? 0 : when;
    }

    if (innconf->storeonxref) {
        start = wire_findheader(text, size, "Xref", true);
        end = start != NULL ? wire_endheader(start, text + size - 1) : NULL;
        while (start != NULL && end != NULL && start < end && *start != ' ')
            start++;
        if (start != NULL && end != NULL && start < end)
            start++; /* past the host name */
        else
            start = NULL;
    } else {
        start = wire_findheader(text, size, "Newsgroups", true);
        end = start != NULL ? wire_endheader(start, text + size - 1) : NULL;
    }
    if (start == NULL || end == NULL) {
        fprintf(stderr, "%s: no %s header field in %s\n", ME,
                innconf->storeonxref ? "usable Xref" : "Newsgroups",
                text_token.c_str());
        return false;
    }
    newart.groups = const_cast<char *>(start);
    newart.groupslen = end - start;

    /* Store the new copy first: if that fails the old one is untouched. */
    TOKEN newtoken = SMstore(newart);
    if (newtoken.type == TOKEN_EMPTY) {
        fprintf(stderr, "%s: cant store article %s: %s\n", ME, text_token.c_str(),
                SMerrorstr);
        return false;
    }
    if (!SMcancel(token)) {
        fprintf(stderr, "%s: stored %s as %s but cant cancel the old copy: %s\n",
                ME, text_token.c_str(), TokenToText(newtoken), SMerrorstr);
        return false;
    }
    printf("refiled %s ", TokenToText(token));
    printf("to %s\n", TokenToText(newtoken));
    return true;
}

int
main(int argc UNUSED, char *argv[])
{
    bool one = true;
    bool ok = true;
    char buff[SMBUF];

    ME = argv[0];

    if (!innconf_read(NULL))
        exit(1);

    if (!SMsetup(SM_PREOPEN, &one) || !SMsetup(SM_RDWR, (void *) &one)) {
        fprintf(stderr, "%s: can't init storage manager\n", ME);
        exit(1);
    }
    if (!SMinit()) {
        fprintf(stderr, "%s: can't init storage manager: %s\n", ME,
                SMerrorstr);
        exit(1);
    }
    while (fgets(buff, SMBUF, stdin)) {
        if (!ProcessLine(buff))
            ok = false;
    }
    SMshutdown();
    printf("\nYou will now need to rebuild history and overview for the moved"
           "\narticles to be visible again.\n");
    exit(ok ? 0 : 1);
}
