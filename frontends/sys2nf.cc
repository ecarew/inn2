/*
**  Read a C News "sys" file and split it up into a set of INN
**  newsfeeds entries.  Also works with B News.
**
**  Once done, edit all files that have HELP or all in them.
**  Review all files, anyway.
*/

#include "portable/system.h"

#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/nntp.h"
#include "inn/paths.h"

#define TEMPFILE ":tmp"
static std::vector<std::string> Groups;

/*
**  Fill in the Groups array with the names of all active newsgroups.
*/
static void
ReadActive(char *act)
{
    FILE *F;
    int i;
    char buff[BUFSIZ];
    char *p;

    /* Open file, count lines. */
    if ((F = fopen(act, "r")) == NULL) {
        perror(act);
        exit(1);
    }
    for (i = 0; fgets(buff, sizeof buff, F) != NULL; i++)
        continue;
    Groups.reserve(i);

    /* Fill in each word. */
    rewind(F);
    while (fgets(buff, sizeof buff, F) != NULL) {
        if ((p = strchr(buff, ' ')) != NULL)
            *p = '\0';
        Groups.emplace_back(buff);
    }
    fclose(F);
}


/*
**  Read in the sys file and turn it into an array of strings, one
**  per continued line.
*/
static std::vector<std::string>
ReadSys(const char *sys)
{
    char *p;
    char *to;
    char *site;
    int i;
    char *data;
    std::vector<std::string> strings;

    /* Read in the file, get rough count. */
    if ((data = ReadInFile(sys, (struct stat *) NULL)) == NULL) {
        perror(sys);
        exit(1);
    }
    for (p = data, i = 0; (p = strchr(p, '\n')) != NULL; p++, i++)
        continue;

    /* Scan the file, glue all multi-line entries. */
    strings.reserve(i + 1);
    for (to = p = data; *p;) {
        for (site = to; *p;) {
            if (*p == '\n') {
                p++;
                *to = '\0';
                break;
            }
            if (*p == '\\' && p[1] == '\n')
                while (*++p && isspace((unsigned char) *p))
                    continue;
            else
                *to++ = *p++;
        }
        *to++ = '\0';
        if (*site == '\0')
            continue;
        strings.emplace_back(site);
    }
    free(data);
    return strings;
}


/*
**  Do we have a name that's a prefix for more than one newsgroup?
**  For "foo.bar", we must find more than one "foo.bar" or "foo.bar."
*/
static bool
GroupPrefix(const char *p)
{
    int count;
    size_t i;

    if (strchr(p, '.') == NULL)
        return false;
    i = strlen(p);
    count = 0;
    for (const std::string &g : Groups)
        if (g == p || (g.compare(0, i, p) == 0 && g.size() > i && g[i] == '.'))
            count++;
    return count > 1;
}


/*
**  Step through the old subscription list, try to update each one in
**  turn.
*/
static void
DoSub(FILE *F, char *p)
{
    const char *s;
    size_t len, i;
    bool matched;
    bool SawBang;
    bool SawAll;

    /* Distributions, not newsgroups. */
    static const char *const distributions[] = {"world", "na",  "usa",  "inet",
                                                "mod",   "net", "local"};

    /* Newsgroup hierarchies. */
    static const char *const hierarchies[] = {
        "comp", "misc",   "news",     "rec",   "sci",   "soc",    "talk",
        "alt",  "bionet", "bit",      "biz",   "clari", "ddn",    "gnu",
        "ieee", "k12",    "pubnet",   "trial", "u3b",   "vmsnet",

        "ba",   "ca",     "dc",       "ne",    "ny",    "tx",

        "info", "mail",   "opinions", "uunet"};

    if ((s = strtok(p, ",")) == NULL)
        return;

    fprintf(F, "!*");
    len = 8 + 1 + 2;
    do {
        for (matched = false, i = 0; i < ARRAY_SIZE(distributions); i++)
            if (strcasecmp(s, distributions[i]) == 0) {
                matched = true;
                break;
            }
        if (matched)
            continue;

        if (innconf->mergetogroups)
            if (strcmp(s, "!to") == 0 || strncmp(s, "to.", 3) == 0)
                continue;

        putc(',', F);
        len++;

        if (len + strlen(s) + 3 > 72) {
            fprintf(F, "\\\n\t    ");
            len = 12;
        }

        SawBang = *s == '!';
        if (SawBang) {
            putc('!', F);
            len++;
            s++;
        }

        SawAll = (strcasecmp(s, "all") == 0);
        if (SawAll)
            s = SawBang ? "*" : "*,!control,!control.*";
        len += strlen(s);
        fprintf(F, "%s", s);

        if (SawAll)
            ;
        else {
            for (matched = false, i = 0; i < ARRAY_SIZE(distributions); i++)
                if (strcmp(s, hierarchies[i]) == 0) {
                    matched = true;
                    break;
                }

            if (matched) {
                fprintf(F, ".*");
                len += 2;
            } else if (GroupPrefix(s)) {
                putc('*', F);
                len++;
            }
        }
    } while ((s = strtok((char *) NULL, ",")) != NULL);
}


int
main(int ac, char *av[])
{
    FILE *F;
    FILE *out;
    std::vector<std::string> sites;
    size_t next = 0;
    char buff[256];
    char *act;
    const char *dir;
    const char *sys;
    int i;
    size_t j;

    if (!innconf_read(NULL))
        exit(1);
    /* Set defaults. */
    act = concatpath(innconf->pathdb, INN_PATH_ACTIVE);
    sys = "sys";
    dir = "feeds";
    while ((i = getopt(ac, av, "a:s:d:")) != EOF)
        switch (i) {
        default:
            exit(1);
            /* NOTREACHED */
        case 'a':
            act = optarg;
            break;
        case 'd':
            dir = optarg;
            break;
        case 's':
            sys = optarg;
            break;
        }

    sites = ReadSys(sys);
    ReadActive(act);
    if (mkdir(dir, 0777) < 0 && errno != EEXIST) {
        perror(dir);
        exit(1);
    }
    if (chdir(dir) < 0) {
        perror("chdir");
        exit(1);
    }
    for (;;) {
        /* Copy comment lines; stop at the next entry. */
        if (next == sites.size())
            break;
        F = fopen(TEMPFILE, "w");
        while (next < sites.size() && sites[next][0] == '#')
            fprintf(F, "%s\n", sites[next++].c_str());
        if (next == sites.size()) {
            fclose(F);
            break;
        }
        const std::string &entry = sites[next++];

        /* site:subscriptions:flags:command; the last takes the rest of the
           line, colons and all, and each missing field is "HELP". */
        std::vector<std::string> fields;
        size_t pos = 0;
        for (int k = 0; k < 3; k++) {
            size_t colon = entry.find(':', pos);
            if (colon == std::string::npos)
                break;
            fields.push_back(entry.substr(pos, colon - pos));
            pos = colon + 1;
        }
        fields.push_back(entry.substr(pos));
        while (fields.size() < 4)
            fields.emplace_back("HELP");
        const std::string &site = fields[0];
        std::string subs = fields[1]; /* DoSub cuts it up with strtok */
        const std::string &flags = fields[2];
        const std::string &command = fields[3];

        /* Write the fields. */
        fprintf(F, "%s\\\n", site.c_str());
        fprintf(F, "\t:");
        DoSub(F, subs.data());
        fprintf(F, "\\\n");
        if (flags == "n")
            fprintf(F, "\t:Tf,Wnm\\\n");
        else
            fprintf(F, "\t:HELP%s\\\n", flags.c_str());
        fprintf(F, "\t:%s\n", command.c_str());
        if (ferror(F) != 0) {
            perror(TEMPFILE);
            fclose(F);
            exit(1);
        }
        if (fclose(F) == EOF) {
            perror(TEMPFILE);
            exit(1);
        }

        /* The site file is named for the site, up to a '/' or ':'. */
        const std::string sitename =
            entry.substr(0, entry.find_first_of("/:"));

        /* Append temp file to site file. */
        if ((F = fopen(TEMPFILE, "r")) == NULL) {
            perror(TEMPFILE);
            exit(1);
        }
        if ((out = xfopena(sitename.c_str())) == NULL) {
            perror(sitename.c_str());
            exit(1);
        }
        while ((j = fread(buff, 1, sizeof buff, F)) > 0)
            if (fwrite(buff, 1, j, out) != j) {
                perror(sitename.c_str());
                exit(1);
            }
        fclose(F);
        if (fclose(out) == EOF) {
            perror(sitename.c_str());
            exit(1);
        }

        if (unlink(TEMPFILE) < 0)
            perror("can't unlink temp file");
    }

    exit(0);

    /* NOTREACHED */
    return 1;
}
