/*
**  Declarations of functions to ensure running as "news" user/group.
**
**  By Ivan Shmakov, 2007.
**  This code is in the public domain.
*/

#ifndef INN_NEWSUSER_H
#define INN_NEWSUSER_H

#include "inn/portable-macros.h"
#include "inn/portable-stdbool.h"

#include <sys/types.h> /* uid_t, gid_t */

BEGIN_DECLS

int get_news_uid_gid(uid_t *uid, gid_t *gid, bool may_die);

/* The following functions die() on failure. */
void ensure_news_user(bool may_setuid);
void ensure_news_grp(bool may_setgid);
void ensure_news_user_grp(bool may_setuid, bool may_setgid);

/* Die if running as root (effective UID 0).  For programs that write INN's
   data and should never create root-owned files in it. */
void ensure_not_root(void);

END_DECLS

#endif /* INN_NEWSUSER_H */
