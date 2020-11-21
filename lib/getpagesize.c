/*  $Id: getpagesize.c 10397 2020-11-12 20:22:51Z iulius $
**
**  Replacement for a missing getpagesize.
**
**  Provides getpagesize implemented in terms of sysconf for those systems
**  that don't have the getpagesize function.  Defaults to a page size of 16KB
**  if sysconf isn't available either.
*/

#include "config.h"
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

int
getpagesize(void)
{
    int pagesize;

#ifdef _SC_PAGESIZE
    pagesize = sysconf(_SC_PAGESIZE);
#else
    pagesize = 16 * 1024;
#endif
    return pagesize;
}
