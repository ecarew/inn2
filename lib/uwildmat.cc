/*
**  uwildmat: the previous wildmat interface, kept for programs outside INN.
**
**  INN itself uses nativemat (lib/nativemat.cc), which replaced the uwildmat
**  matcher in November 2026.  These wrappers call nativemat, so they match
**  exactly as it does, including its fix for an escaped \[ in a list.  They
**  are declared deprecated in inn/libinn.h; see libinn_nativemat(3).
*/

#include "portable/system.h"

#include "inn/libinn.h"

bool
uwildmat(const char *text, const char *pat)
{
    return nativemat(text, pat);
}

bool
uwildmat_simple(const char *text, const char *pat)
{
    return nativemat_simple(text, pat);
}

enum uwildmat
uwildmat_poison(const char *text, const char *pat)
{
    return static_cast<enum uwildmat>(nativemat_poison(text, pat));
}
