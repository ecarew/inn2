/*
**  Dispatch cvectors of commands to functions.
**
**  This is a generic command dispatching system designed primary to handle
**  dispatching NNTP commands to functions that handle them, for NNTP server
**  software.
*/

#include "portable/system.h"

#include <algorithm>

#include "inn/dispatch.h"
#include "inn/vector.h"


/*
**  The dispatch function.  Takes a command (as a struct cvector), the
**  dispatch table (a SORTED array of dispatch structs), the number of
**  elements in the table, the callback function for unknown commands, the
**  callback function for syntax errors (commands called with the wrong number
**  of arguments), and an opaque void * that will be passed to the callback
**  functions.
*/
void
dispatch(struct cvector *command, const struct dispatch *table, size_t count,
         dispatch_func unknown, dispatch_func syntax, void *cookie)
{
    const struct dispatch *rule, *end = table + count;
    int argc = command->count - 1;

    if (argc < 0) {
        (*unknown)(command, cookie);
        return;
    }
    /* The table is sorted by command name, ignoring case. */
    rule = std::lower_bound(table, end, command->strings[0],
                            [](const struct dispatch &r, const char *name) {
                                return strcasecmp(r.command, name) < 0;
                            });
    if (rule == end || strcasecmp(rule->command, command->strings[0]) != 0)
        (*unknown)(command, cookie);
    else if (argc < rule->min_args || argc > rule->max_args)
        (*syntax)(command, cookie);
    else
        (*rule->callback)(command, cookie);
}
