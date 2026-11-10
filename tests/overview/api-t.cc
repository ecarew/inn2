/* Test suite for overview API. */

#include "portable/system.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <string>
#include <unordered_map>

#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/overview.h"
#include "inn/storage.h"
#include "inn/vector.h"
#include "tap/basic.h"

/* Used as the artificial token for all articles inserted into overview. */
static const TOKEN faketoken = {1, 1, ""};

/* What a group should hold once the data is loaded. */
struct group {
    unsigned long count;
    unsigned long low;
    unsigned long high;
};
using group_map = std::unordered_map<std::string, group>;

/* Build a stripped-down innconf struct that contains only those settings that
   the overview backend cares about.  (May still be missing additional bits
   for ovdb.) */
static void
fake_innconf(void)
{
    if (innconf != nullptr) {
        free(innconf->ovmethod);
        free(innconf->pathdb);
        free(innconf->pathetc);
        free(innconf->pathoverview);
        free(innconf->pathrun);
        free(innconf);
    }
    innconf = xmalloc(sizeof(*innconf));
    memset(innconf, 0, sizeof(*innconf));
    innconf->enableoverview = true;
    innconf->groupbaseexpiry = true;
    innconf->keepmmappedthreshold = 1024;
    innconf->nfsreader = false;
    innconf->overcachesize = 20;
    innconf->ovflushcount = 50;
    innconf->ovgrouppat = nullptr;
    innconf->pathdb = xstrdup("ov-tmp");
    innconf->pathetc = xstrdup("etc");
    innconf->pathoverview = xstrdup("ov-tmp");
    innconf->pathrun = xstrdup("ov-tmp");
}

/* Initialize an empty buffindexed buffer. */
static void
overview_init_buffindexed(void)
{
    int fd, i;
    char zero[1024];

    fd = open("ov-tmp/buffer", O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (fd < 0)
        sysdie("Cannot create ov-tmp/buffer");
    memset(zero, 0, sizeof(zero));
    for (i = 0; i < 1024; i++)
        if (write(fd, zero, sizeof(zero)) < (ssize_t) sizeof(zero))
            sysdie("Cannot write to ov-tmp/buffer");
    close(fd);
}

/* Initialize the overview database. */
static struct overview *
overview_init(void)
{
    if (system("/bin/rm -rf ov-tmp") < 0)
        sysdie("Cannot rm ov-tmp");
    if (mkdir("ov-tmp", 0755))
        sysdie("Cannot mkdir ov-tmp");
    if (strcmp(innconf->ovmethod, "buffindexed") == 0)
        overview_init_buffindexed();
    return overview_open(OV_READ | OV_WRITE);
}

/* Check to be sure that the line wasn't too long, and then parse the
   beginning of the line from one of our data files, setting the article
   number (via the passed pointer) and returning a pointer to the beginning of
   the real overview data.  This function nul-terminates the group name and
   leaves it at the beginning of the buffer.  (Ugly interface, but it's just a
   test suite.) */
static char *
overview_data_parse(char *data, unsigned long *artnum)
{
    char *start;
    size_t length;

    length = strlen(data);
    if (data[length - 1] != '\n')
        die("Line too long in input data");
    data[length - 1] = '\0';

    start = strchr(data, ':');
    if (start == nullptr)
        die("No colon found in input data");
    *start = '\0';
    start++;
    *artnum = strtoul(start, nullptr, 10);
    if (*artnum == 0)
        die("Cannot parse article number in input data");
    return start;
}

/* Load an empty overview database from a file, in the process populating a
   map with each group, the high water mark, and the count of messages
   that should be in the group.  Returns the map on success and dies on
   failure.  Takes the name of the data file to load and the overview
   struct. */
static group_map
overview_load(const char *data, struct overview *overview)
{
    group_map groups;
    FILE *overdata;
    char buffer[4096];
    char *start;
    unsigned long artnum;
    struct overview_group stats = {0, 0, 0, NF_FLAG_OK};
    struct overview_data article;

    /* Run through the overview data.  Each time we see a group, we update our
       stored information about that group, which we'll use for verification
       later.  We store that in a local map. */
    overdata = fopen(data, "r");
    if (overdata == nullptr)
        sysdie("Cannot open %s for reading", data);
    while (fgets(buffer, sizeof(buffer), overdata) != nullptr) {
        start = overview_data_parse(buffer, &artnum);

        /* The overview API adds the article number, so strip that out before
           storing the data (overview_data_parse leaves it in because we want
           it in for data validation). */
        start = strchr(start, '\t');
        if (start == nullptr)
            die("No tab found after number in input data");
        *start = '\0';
        start++;

        /* See if we've already seen this group.  If not, create it in the
           overview and the map; otherwise, update our local map entry. */
        auto found = groups.find(buffer);
        if (found == groups.end()) {
            groups.emplace(buffer, group{1, artnum, artnum});
            if (!overview_group_add(overview, buffer, &stats))
                die("Cannot insert group %s into overview", buffer);
        } else {
            group &g = found->second;

            g.count++;
            g.low = (artnum < g.low) ? artnum : g.low;
            g.high = (artnum > g.high) ? artnum : g.high;
        }

        /* Do the actual insert of the data.  Note that we set the arrival
           time and expires time in a deterministic fashion so that we can
           check later if that data is being stored properly. */
        article.number = artnum;
        article.overview = start;
        article.overlen = strlen(start);
        article.token = faketoken;
        article.arrived = artnum * 10;
        article.expires = (artnum % 5 == 0) ? artnum * 100 : artnum;
        if (!overview_add(overview, buffer, &article))
            die("Cannot insert %s:%lu into overview", buffer, artnum);
    }
    fclose(overdata);
    return groups;
}

/* Verify that all of the group data looks correct; this is low mark, high
   mark, and article count.  Returns true if all the data is right, false
   otherwise. */
static bool
overview_verify_groups(const group_map &groups, struct overview *overview)
{
    bool status = true;

    for (const auto &entry : groups) {
        const char *name = entry.first.c_str();
        const group &g = entry.second;
        struct overview_group stats;

        if (!overview_group(overview, name, &stats)) {
            warn("Unable to get data for %s", name);
            status = false;
            continue;
        }
        if (stats.low != g.low) {
            warn("Low article wrong for %s: %lu != %lu", name, stats.low,
                 g.low);
            status = false;
        }
        if (stats.high != g.high) {
            warn("High article wrong for %s: %lu != %lu", name, stats.high,
                 g.high);
            status = false;
        }
        if (stats.count != g.count) {
            warn("Article count wrong for %s: %lu != %lu", name, stats.count,
                 g.count);
            status = false;
        }
        if (stats.flag != NF_FLAG_OK) {
            warn("Flag wrong for %s: %c != %c", name, stats.flag, NF_FLAG_OK);
            status = false;
        }
    }
    return status;
}

/* Verify the components of the overview data for a particular entry. */
static bool
check_data(const char *group, ARTNUM artnum, const char *expected,
           struct overview_data *data)
{
    bool status = true;
    time_t expires UNUSED; // See below why it is still unused.
    char *saw;

    if (artnum != data->number) {
        warn("Incorrect article number in search for %s:%lu: %lu != %lu",
             group, artnum, data->number, artnum);
        status = false;
    }
    if (strlen(expected) != data->overlen - 2) {
        warn("Length wrong for %s:%lu: %lu != %lu", group, artnum,
             (unsigned long) data->overlen, (unsigned long) strlen(expected));
        status = false;
    }
    if (memcmp(&data->token, &faketoken, sizeof(faketoken)) != 0) {
        warn("Token wrong for %s:%lu", group, artnum);
        status = false;
    }
    if (memcmp(expected, data->overview, data->overlen - 2) != 0) {
        warn("Data mismatch for %s:%lu", group, artnum);
        saw = xstrndup(data->overview, data->overlen);
        warn("====\n%s====\n%s====", expected, saw);
        free(saw);
        status = false;
    }
    if (memcmp("\r\n", data->overview + data->overlen - 2, 2) != 0) {
        warn("Missing CRLF after data for %s:%lu", group, artnum);
        status = false;
    }
    if ((unsigned long) data->arrived != artnum * 10) {
        warn("Arrival time wrong for %s:%lu: %lu != %lu", group, artnum,
             (unsigned long) data->arrived, artnum * 10);
        status = false;
    }

    /* expires is always 0 for right now because the underlying API doesn't
       return it; this will change when the new API has been pushed all the
       way down to the overview implementations. */
    expires = (artnum % 5 == 0) ? artnum * 100 : artnum;
    if (data->expires != 0) {
        warn("Expires time wrong for %s:%lu: %lu != %lu", group, artnum,
             (unsigned long) data->expires, 0UL);
        status = false;
    }
    return status;
}

/* Read through the data again, looking up each article as we go and verifying
   that the data stored in overview is the same as the data we put there.  Do
   this two ways each time, once via overview_token and once via
   overview_search.  Return true if everything checks out, false otherwise.
   Takes the path to the data file and the overview struct. */
static bool
overview_verify_data(const char *data, struct overview *overview)
{
    FILE *overdata;
    char buffer[4096];
    char *start;
    unsigned long artnum;
    TOKEN token;
    struct ov_search *search;
    struct overview_data article;
    bool status = true;

    overdata = fopen(data, "r");
    if (overdata == nullptr)
        sysdie("Cannot open %s for reading", data);
    while (fgets(buffer, sizeof(buffer), overdata) != nullptr) {
        start = overview_data_parse(buffer, &artnum);

        /* Now check that the overview data is correct for that group. */
        if (!overview_token(overview, buffer, artnum, &token)) {
            warn("No overview data found for %s:%lu", buffer, artnum);
            status = false;
            continue;
        }
        if (memcmp(&token, &faketoken, sizeof(token)) != 0) {
            warn("Token wrong for %s:%lu", buffer, artnum);
            status = false;
        }

        /* Do the same thing, except use search. */
        search = overview_search_open(overview, buffer, artnum, artnum);
        if (search == nullptr) {
            warn("Unable to open search for %s:%lu", buffer, artnum);
            status = false;
            continue;
        }
        if (!overview_search(overview, search, &article)) {
            warn("No overview data found for %s:%lu", buffer, artnum);
            status = false;
            continue;
        }
        if (!check_data(buffer, artnum, start, &article))
            status = false;
        if (overview_search(overview, search, &article)) {
            warn("Unexpected article found for %s:%lu", buffer, artnum);
            status = false;
        }
        overview_search_close(overview, search);
    }
    fclose(overdata);
    return status;
}

/* Try an overview search and verify that all of the data is returned in the
   right order.  The first group mentioned in the provided data file will be
   the group the search is done in, and the search will cover all articles
   from the second article to the second-to-the-last article in the group.
   Returns true if everything checks out, false otherwise. */
static bool
overview_verify_search(const char *data, struct overview *overview)
{
    unsigned long artnum, i;
    unsigned long start = 0;
    unsigned long end = 0;
    unsigned long last = 0;
    struct vector *expected;
    char *group, *line;
    FILE *overdata;
    char buffer[4096];
    struct ov_search *search;
    struct overview_data article;
    bool status = true;

    overdata = fopen(data, "r");
    if (overdata == nullptr)
        sysdie("Cannot open %s for reading", data);
    expected = vector_new();
    if (fgets(buffer, sizeof(buffer), overdata) == nullptr) {
        fclose(overdata);
        die("Unexpected end of file in %s", data);
    }
    overview_data_parse(buffer, &artnum);
    group = xstrdup(buffer);
    while (fgets(buffer, sizeof(buffer), overdata) != nullptr) {
        line = overview_data_parse(buffer, &artnum);
        if (strcmp(group, buffer) != 0)
            continue;
        vector_add(expected, line);
        if (start == 0)
            start = artnum;
        end = last;
        last = artnum;
    }
    search = overview_search_open(overview, group, start, end);
    if (search == nullptr) {
        warn("Unable to open search for %s:%lu", buffer, start);
        free(group);
        vector_free(expected);
        fclose(overdata);
        return false;
    }
    i = 0;
    while (overview_search(overview, search, &article)) {
        if (!check_data(group, article.number, expected->strings[i], &article))
            status = false;
        i++;
    }
    overview_search_close(overview, search);
    if (article.number != end) {
        warn("End of search in %s wrong: %lu != %lu", group, article.number,
             end);
        status = false;
    }
    if (i != expected->count - 1) {
        warn("Didn't see all expected entries in %s", group);
        status = false;
    }
    free(group);
    vector_free(expected);
    fclose(overdata);
    return status;
}

/* Try an overview search and verify that all of the data is returned in the
   right order.  The search will cover everything from article 1 to the
   highest numbered article plus one.  There were some problems with a search
   low water mark lower than the base of the group.  Returns true if
   everything checks out, false otherwise. */
static bool
overview_verify_full_search(const char *data, struct overview *overview)
{
    unsigned long artnum, i;
    unsigned long end = 0;
    struct vector *expected;
    char *line;
    char *group = nullptr;
    FILE *overdata;
    char buffer[4096];
    struct ov_search *search;
    struct overview_data article;
    bool status = true;

    overdata = fopen(data, "r");
    if (overdata == nullptr)
        sysdie("Cannot open %s for reading", data);
    expected = vector_new();
    while (fgets(buffer, sizeof(buffer), overdata) != nullptr) {
        line = overview_data_parse(buffer, &artnum);
        if (group == nullptr)
            group = xstrdup(buffer);
        vector_add(expected, line);
        end = artnum;
    }
    search = overview_search_open(overview, group, 1, end + 1);
    if (search == nullptr) {
        warn("Unable to open full search for %s", group);
        free(group);
        vector_free(expected);
        fclose(overdata);
        return false;
    }
    i = 0;
    while (overview_search(overview, search, &article)) {
        if (!check_data(group, article.number, expected->strings[i], &article))
            status = false;
        i++;
    }
    overview_search_close(overview, search);
    if (article.number != end) {
        warn("End of search in %s wrong: %lu != %lu", group, article.number,
             end);
        status = false;
    }
    if (i != expected->count) {
        warn("Didn't see all expected entries in %s", group);
        status = false;
    }
    free(group);
    vector_free(expected);
    fclose(overdata);
    return status;
}

/* Run the tests on a particular overview setup.  Expects to be called
   multiple times with different inn.conf configurations to test the various
   iterations of overview support.  Takes the current test number and returns
   the next test number. */
static int
overview_tests(int n)
{
    group_map groups;
    struct overview *overview;
    struct ov_search *search;
    struct overview_data data;
    TOKEN token;

    overview = overview_init();
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, true);

    groups = overview_load("overview/basic", overview);
    ok(n++, true);
    ok(n++, overview_verify_groups(groups, overview));
    ok(n++, overview_verify_data("overview/basic", overview));
    ok(n++, overview_verify_search("overview/basic", overview));
    overview_close(overview);
    ok(n++, true);

    overview = overview_init();
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, true);

    groups = overview_load("overview/reversed", overview);
    ok(n++, true);
    ok(n++, overview_verify_groups(groups, overview));
    ok(n++, overview_verify_data("overview/basic", overview));
    ok(n++, overview_verify_search("overview/basic", overview));
    overview_close(overview);
    ok(n++, true);

    overview = overview_init();
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, true);

    groups = overview_load("overview/high-numbered", overview);
    ok(n++, true);
    ok(n++, overview_verify_data("overview/high-numbered", overview));
    ok(n++, overview_verify_full_search("overview/high-numbered", overview));
    if (strcmp(innconf->ovmethod, "buffindexed") == 0) {
        skip_block(n, 6, "buffindexed doesn't support cancel");
        n += 6;
    } else {
        ok(n++, overview_cancel(overview, "example.test", 7498));
        ok(n++, !overview_token(overview, "example.test", 7498, &token));
        search = overview_search_open(overview, "example.test", 7498, 7499);
        ok(n++, search != nullptr);
        ok(n++, overview_search(overview, search, &data));
        ok_int(n++, 7499, data.number);
        ok(n++, !overview_search(overview, search, &data));
        overview_search_close(overview, search);
    }
    overview_close(overview);
    ok(n++, true);

    overview = overview_init();
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, true);

    groups = overview_load("overview/bogus", overview);
    ok(n++, true);
    ok(n++, overview_verify_data("overview/bogus", overview));
    overview_close(overview);
    if (system("/bin/rm -rf ov-tmp") < 0)
        sysdie("Cannot rm ov-tmp");
    ok(n++, true);
    return n;
}

/* Run the tests on a particular overview setup.  This is a copy of
   overview_tests that closes and reopens the overview without mmap to test
   tradindexedmmap.  Takes the current test and returns the next test
   number. */
static int
overview_mmap_tests(int n)
{
    group_map groups;
    struct overview *overview;

    overview = overview_init();
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, true);

    groups = overview_load("overview/basic", overview);
    ok(n++, true);
    overview_close(overview);
    innconf->tradindexedmmap = false;
    overview = overview_open(OV_READ);
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, overview_verify_groups(groups, overview));
    ok(n++, overview_verify_data("overview/basic", overview));
    ok(n++, overview_verify_search("overview/basic", overview));
    overview_close(overview);
    ok(n++, true);

    innconf->tradindexedmmap = true;
    overview = overview_init();
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, true);

    groups = overview_load("overview/reversed", overview);
    ok(n++, true);
    overview_close(overview);
    innconf->tradindexedmmap = false;
    overview = overview_open(OV_READ);
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, overview_verify_groups(groups, overview));
    ok(n++, overview_verify_data("overview/basic", overview));
    ok(n++, overview_verify_search("overview/basic", overview));
    overview_close(overview);
    ok(n++, true);

    innconf->tradindexedmmap = true;
    overview = overview_init();
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, true);

    groups = overview_load("overview/high-numbered", overview);
    ok(n++, true);
    overview_close(overview);
    innconf->tradindexedmmap = false;
    overview = overview_open(OV_READ);
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, overview_verify_data("overview/high-numbered", overview));
    ok(n++, overview_verify_full_search("overview/high-numbered", overview));
    overview_close(overview);
    ok(n++, true);

    innconf->tradindexedmmap = true;
    overview = overview_init();
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, true);

    groups = overview_load("overview/bogus", overview);
    ok(n++, true);
    overview_close(overview);
    innconf->tradindexedmmap = false;
    overview = overview_open(OV_READ);
    if (overview == nullptr)
        die("Opening the overview database failed, cannot continue");
    ok(n++, overview_verify_data("overview/bogus", overview));
    overview_close(overview);
    if (system("/bin/rm -rf ov-tmp") < 0)
        sysdie("Cannot rm ov-tmp");
    ok(n++, true);
    return n;
}

int
main(void)
{
    int n = 1;

    if (access("../data/overview/basic", F_OK) == 0) {
        if (chdir("../data") < 0) {
            sysbail("cannot chdir to ../data");
        }
    } else if (access("data/overview/basic", F_OK) == 0) {
        if (chdir("data") < 0) {
            sysbail("cannot chdir to data");
        }
    } else if (access("tests/data/overview/basic", F_OK) == 0) {
        if (chdir("tests/data") < 0) {
            sysbail("cannot chdir to tests/data");
        }
    }

    /* Cancels can't be tested with mmap, so there are only 21 tests there. */
    test_init(27 * 2 + 21);

    fake_innconf();
    innconf->ovmethod = xstrdup("tradindexed");
    innconf->tradindexedmmap = true;
    diag("tradindexed with mmap");
    n = overview_tests(1);

    diag("tradindexed without mmap");
    n = overview_mmap_tests(n);

    free(innconf->ovmethod);
    innconf->ovmethod = xstrdup("buffindexed");
    diag("buffindexed");
    overview_tests(n);

    return 0;
}
