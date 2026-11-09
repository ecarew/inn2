/*
**  Internal history API interface exposed to HISxxx
**
**  Written by Alex Kiernan in 2001.
**
**  Various bug fixes, code and documentation improvements since then
**  in 2001, 2009, 2020, 2021, 2026.
*/

#ifndef HISV6_H
#define HISV6_H

#include "inn/libinn.h"

BEGIN_DECLS

struct token;
struct histopts;
struct history;
struct his_handle;

struct his_handle *hisv6_open(const char *path, int flags, struct history *);

bool hisv6_close(struct his_handle *);

bool hisv6_sync(struct his_handle *);

bool hisv6_lookup(struct his_handle *, const char *key, time_t *arrived,
                  time_t *posted, time_t *expires, struct token *token);

bool hisv6_check(struct his_handle *, const char *key);

bool hisv6_write(struct his_handle *, const char *key, time_t arrived,
                 time_t posted, time_t expires, const struct token *token);

bool hisv6_replace(struct his_handle *, const char *key, time_t arrived,
                   time_t posted, time_t expires, const struct token *token);

bool hisv6_expire(struct his_handle *, const char *, const char *, bool,
                  void *, time_t threshold,
                  bool (*exists)(void *, time_t, time_t, time_t,
                                 struct token *));

bool hisv6_walk(struct his_handle *, const char *, void *,
                bool (*)(void *, const HASH *, time_t, time_t, time_t,
                         const struct token *));

const char *hisv6_error(struct his_handle *);

bool hisv6_remember(struct his_handle *, const char *key, time_t arrived,
                    time_t posted);

bool hisv6_ctl(struct his_handle *, int, void *);

END_DECLS

#endif
