/*
**  Test suite for ARTfindheader, innd's lookup of known header fields by
**  name.
*/

#define LIBTEST_NEW_FORMAT 1

#include "portable/system.h"

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/vector.h"
#include "tap/basic.h"

#include "../../innd/innd.h"

/* Names that must not be found: unknown fields, prefixes and extensions of
   known ones, and the empty name. */
static const char *const unknown[] = {
    "X-Unknown", "", "P", "Pat", "Paths", "Message-I", "Message-IDs",
    "Xref ", " Xref", "_Path", "0Path", "~Path", "Zzz", "Aaa",
};

/* Enough of innconf for ARTsetup (it reads the overview schema). */
static void
fake_innconf(void)
{
    innconf = xmalloc(sizeof(*innconf));
    memset(innconf, 0, sizeof(*innconf));
    innconf->pathetc = xstrdup("../data/etc");
    innconf->extraoverviewadvertised = vector_new();
    innconf->extraoverviewhidden = vector_new();
}

/* Copy name with each letter in the given case: 'l' lower, 'u' upper, 'a'
   alternating. */
static void
recase(char *out, const char *name, char mode)
{
    size_t i;

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char) name[i];

        if (mode == 'l' || (mode == 'a' && i % 2 == 0))
            out[i] = (char) tolower(c);
        else
            out[i] = (char) toupper(c);
    }
    out[i] = '\0';
}

int
main(void)
{
    char name[SMBUF];
    size_t i;
    const char modes[] = "lua";
    size_t m;

    if (access("../data/etc/storage.conf", F_OK) < 0)
        if (access("data/etc/storage.conf", F_OK) == 0)
            if (chdir("innd") != 0)
                sysdie("Cannot cd to innd");
    fake_innconf();
    Log = fopen("/dev/null", "w");
    if (Log == NULL)
        sysdie("Cannot open /dev/null");

    plan(MAX_ARTHEADER * 4 + ARRAY_SIZE(unknown));
    ARTsetup();

    /* Every known name is found, as given and in three other cases, and
       finds its own entry. */
    for (i = 0; i < MAX_ARTHEADER; i++) {
        ok(ARTfindheader(ARTheaders[i].Name) == &ARTheaders[i], "%s",
           ARTheaders[i].Name);
        for (m = 0; m < 3; m++) {
            recase(name, ARTheaders[i].Name, modes[m]);
            ok(ARTfindheader(name) == &ARTheaders[i], "%s", name);
        }
    }
    for (i = 0; i < ARRAY_SIZE(unknown); i++)
        ok(ARTfindheader(unknown[i]) == NULL, "not found: \"%s\"",
           unknown[i]);

    ARTclose();
    return 0;
}
