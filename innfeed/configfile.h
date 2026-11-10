/*
**  Interface to innfeed's configuration file parser.
**
**  Written by James Brister <brister@vix.com>
*/

#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include "portable/macros.h"
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

/* A function called after the configuration is (re)loaded.  Its argument is
   where to report problems: stderr when only checking the configuration
   (-C), otherwise NULL (report through syslog). */
using ConfigLoadCbk = int (*)(std::FILE *);

typedef enum {
    intval,
    charval,
    boolval,
    realval,
    stringval,
    scopeval
} tag;

struct value;

/* A scope: the top level, or a peer or group block.  Owns its values. */
struct scope {
    value *me = nullptr;        /* the value that names this scope */
    std::string scope_type;     /* "peer", "group" or "" (lower case) */
    std::vector<value *> values;
    scope *parent = nullptr;
};

/* A named setting.  The type says which member holds it: string_val for a
   string, the union for the other types. */
struct value {
    std::string name;
    scope *myscope = nullptr;
    tag type = intval;
    union {
        char char_val;
        double real_val;
        int bool_val;
        long int_val;
        scope *scope_val;
    } v{};
    std::string string_val;
};

/* Variables to export. */
extern scope *topScope;
extern std::string errbuff;
extern int lineCount;

int getReal(scope *s, const char *name, double *rval, int inherit);
int getInteger(scope *s, const char *name, long *rval, int inherit);
int getBool(scope *s, const char *name, int *rval, int inherit);
bool getString(scope *s, const char *name, std::string &rval, int inherit);
/* TRANSITIONAL: the old form, a copy the caller frees; removed once the
   callers take a std::string. */
int getString(scope *s, const char *name, char **rval, int inherit);

void freeScopeTree(scope *s);
std::string addInteger(scope *s, const char *name, long val);
std::string addChar(scope *s, const char *name, char val);
std::string addBoolean(scope *s, const char *name, int val);
std::string addReal(scope *s, const char *name, double val);
std::string addString(scope *s, const char *name, std::string_view val);
/* Set name's string value in s, or add it if s does not have it. */
void setString(scope *s, const char *name, std::string_view val);
scope *findScope(scope *s, const char *name, int mustExist);
value *findValue(scope *s, const char *name, int inherit);
value *findPeer(const char *name);
value *getNextPeer(int *cookie);
void configAddLoadCallback(ConfigLoadCbk func, std::FILE *arg);
void configRemoveLoadCallback(ConfigLoadCbk func);
int readConfig(const char *file, FILE *errorDest, int justCheck, int dump);
int buildPeerTable(FILE *fp, scope *currScope);
void configCleanup(void);

/* For the lexer: keep a token's text for the rest of the parse and return a
   pointer to it, as yacc's value stack holds only plain pointers. */
const char *configToken(std::string text);

#define ARTICLE_TIMEOUT     "article-timeout"
#define BACKLOG_LIMIT       "backlog-limit"
#define INITIAL_CONNECTIONS "initial-connections"
#define IP_NAME             "ip-name"
#define MAX_CONNECTIONS     "max-connections"
#define MAX_QUEUE_SIZE      "max-queue-size"
#define NO_CHECK_HIGH       "no-check-high"
#define NO_CHECK_LOW        "no-check-low"
#define PORT_NUMBER         "port-number"
#define RESP_TIMEOUT        "response-timeout"
#define STREAMING           "streaming"
#define DROP_DEFERRED       "drop-deferred"

#define ISPEER(V)  (ISSCOPE(V) && (V)->v.scope_val->scope_type == "peer")
#define ISSCOPE(V) (V->type == scopeval)

#define INHERIT    1
#define NO_INHERIT 0

#endif /* CONFIGFILE_H */
