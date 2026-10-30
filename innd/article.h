/*
**  innd's C++ state for the article being received on a channel
**  (CONVERSION_NOTES.md, "innd's in-memory article").
**
**  ARTDATA stays a C struct, because innd.h is also compiled as C by the
**  tests and chan.cc copies channels as plain structs; it holds a pointer
**  to an ArticleState, which C code sees as an incomplete type.  Each
**  channel owns one: CHANcreate creates it (or keeps the one the slot
**  already has) and CHANclose deletes it.  ARTprepare calls clear() before
**  each article.  The fields move here from ARTDATA in later steps.
*/

#ifndef INND_ARTICLE_H
#define INND_ARTICLE_H 1

struct ArticleState {
    ArticleState() = default;
    ArticleState(const ArticleState &) = delete;
    ArticleState &operator=(const ArticleState &) = delete;

    /* Reset for the next article on the channel. */
    void
    clear()
    {
    }
};

#endif /* !INND_ARTICLE_H */
