/*
**  Tests for the deprecated uwildmat wrappers.
**
**  The matching itself is tested by tests/lib/nativemat-t.c, which holds the
**  former uwildmat suite.  These check that each wrapper passes its
**  arguments and its result through.
*/

#include "portable/system.h"

/* This file tests a deprecated interface on purpose. */
#if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "inn/libinn.h"
#include "tap/basic.h"

int
main(void)
{
    test_init(9);

    ok(1, uwildmat("comp.lang.c", "comp.*"));
    ok(2, !uwildmat("comp.lang.c", "*,!comp.*"));
    ok(3, uwildmat("@ab", "@ab"));
    ok(4, uwildmat_simple("a,b", "a,b"));
    ok(5, !uwildmat_simple("a", "a,b"));
    ok(6, uwildmat_poison("a", "*,@a") == UWILDMAT_POISON);
    ok(7, uwildmat_poison("b", "*,@a") == UWILDMAT_MATCH);
    ok(8, uwildmat_poison("b", "a") == UWILDMAT_FAIL);
    ok(9, uwildmat("x", "\\[,x"));

    return 0;
}
