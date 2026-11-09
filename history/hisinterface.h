/*
**  Interface to history API modules.
**
**  Written by Alex Kiernan in 2001.
**
**  Various bug fixes, code and documentation improvements since then
**  in 2001-2003, 2009, 2020-2022, 2026.
*/

#ifndef HISINTERFACE_H
#define HISINTERFACE_H

#include "config.h"
#include "inn/libinn.h"
#include <sys/types.h>

/* A history method's own state, as its open returns it and the other
   methods take it back: each method's state structure (struct hisv6,
   struct hissqlite) derives from it, so his.cc holds a typed pointer and
   each method converts it back with a checked static_cast.  Outside
   BEGIN_DECLS: an empty struct has no C layout (size 0 in C, 1 in C++),
   and only C++ includes this header. */
struct his_handle {};

BEGIN_DECLS

struct token;
struct histopts;
struct history;

typedef struct hismethod {
    const char *name;
    struct his_handle *(*open)(const char *path, int flags, struct history *);
    bool (*close)(struct his_handle *);
    bool (*sync)(struct his_handle *);
    bool (*lookup)(struct his_handle *, const char *, time_t *, time_t *,
                   time_t *, struct token *);
    bool (*check)(struct his_handle *, const char *);
    bool (*write)(struct his_handle *, const char *, time_t, time_t, time_t,
                  const struct token *);
    bool (*replace)(struct his_handle *, const char *, time_t, time_t, time_t,
                    const struct token *);
    bool (*expire)(struct his_handle *, const char *, const char *, bool,
                   void *, time_t,
                   bool (*)(void *, time_t, time_t, time_t, struct token *));
    bool (*walk)(struct his_handle *, const char *, void *,
                 bool (*)(void *, const HASH *, time_t, time_t, time_t,
                          const struct token *));
    bool (*remember)(struct his_handle *, const char *, time_t, time_t);
    bool (*ctl)(struct his_handle *, int, void *);
} HIS_METHOD;

/* subordinate history manager private methods */
void his_seterror(struct history *, const char *);

enum {
    S_HIScacheadd,
    S_HIScachelookup,
    S_HISsetup,
    S_HISsync,
    S_HISlogstats,
    S_HISclose,
    S_HISfilesfor,
    S_HIShavearticle,
    S_HISwrite,
    S_HISremember,
    S_HIS_MAX
};

/* fine grained history logging */
void his_logger(const char *s, int code);

END_DECLS

#endif
