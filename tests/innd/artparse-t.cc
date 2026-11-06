/*
**  Test suite for ARTparse.
**
**  Written by Russ Allbery in 2004.
**
**  Various bug fixes, code and documentation improvements since then
**  in 2004-2007, 2009, 2010, 2014, 2015, 2021, 2022, 2024, 2026.
*/

#include "portable/system.h"

#include "inn/buffer.h"
#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/vector.h"
#include "inn/wire.h"
#include "tap/basic.h"

#include "../../innd/innd.h"

#include <cstdint>
#include <string>
#include <vector>

/* A table of paths to articles and corresponding error messages. */
static const struct {
    const char *path;
    const char *error;
} articles[] = {
    {"../data/articles/1",              ""                           },
    {"../data/articles/2",              ""                           },
    {"../data/articles/3",              ""                           },
    {"../data/articles/4",              ""                           },
    {"../data/articles/5",              ""                           },
    {"../data/articles/bad-msgid",      ""                           },
    {"../data/articles/bad-subj",       ""                           },
    {"../data/articles/6",
     "437 Article of 8193 bytes exceeds local limit of 8192 bytes"   },
    {"../data/articles/bad-empty",      "437 Empty headers and body" },
    {"../data/articles/bad-hdr-nospc",
     "437 No colon-space in \"Test:<-he: re\" header field"          },
    {"../data/articles/bad-hdr-space",
     "437 Space before colon in \"Test\" header field"               },
    {"../data/articles/bad-hdr-trunc",
     "437 No colon-space in \"Test:\" header field"                  },
    /* Not an error for a relaying agent. */
    {"../data/articles/bad-long-cont",  ""                           },
    /* Not an error for a relaying agent. */
    {"../data/articles/bad-long-hdr",   ""                           },
    {"../data/articles/bad-no-body",    "437 No body"                },
    {"../data/articles/bad-no-header",  "437 No headers"             },
    {"../data/articles/bad-nul-body",   "437 Nul character in body"  },
    {"../data/articles/bad-nul-header", "437 Nul character in header"}
};

/* Create enough of an innconf struct to be able to run ARTparse.  Set
   logipaddr to false so that we don't have to initialize enough in the
   channel to get RChostname working. */
static void
fake_innconf(void)
{
    if (innconf != nullptr) {
        free(innconf->pathetc);
        free(innconf);
    }
    innconf = xmalloc(sizeof(*innconf));
    memset(innconf, 0, sizeof(*innconf));
    innconf->logipaddr = false;
    innconf->maxartsize = 8 * 1024;
    innconf->pathetc = xstrdup("../data/etc");
    innconf->extraoverviewadvertised = vector_new();
    innconf->extraoverviewhidden = vector_new();
}

/* Create a fake channel with just enough data filled in to be able to use it
   to test article parsing. */
static CHANNEL *
fake_channel(void)
{
    CHANNEL *cp;
    static const CHANNEL CHANnull{};

    cp = xmalloc(sizeof(CHANNEL));
    *cp = CHANnull;
    cp->Type = CTnntp;
    cp->State = CSgetheader;
    return cp;
}

/* Initialize things enough to be able to call ARTparse and friends.  This
   only has to be called once. */
static void
initialize(void)
{
    if (access("../data/etc/storage.conf", F_OK) < 0)
        if (access("data/etc/storage.conf", F_OK) == 0)
            if (chdir("innd") != 0)
                sysdie("Cannot cd to innd");
    fake_innconf();
    Log = fopen("/dev/null", "w");
    if (Log == nullptr)
        sysdie("Cannot open /dev/null");
    fdreserve(4);
    /* ARTsetup needs to have a proper Path. */
    buffer_set(&Path, "example.com!others", strlen("example.com!others") + 1);
    Path.used += strlen("example.com!");
    ARTsetup();
}

/* Given the test number, a path to an article and an expected error message
   (which may be ""), create a channel, run the article through ARTparse
   either all at once or, if slow is true, one character at a time, and check
   the result.  If shift is true, shift the start of the article in the buffer
   by a random amount.  Produces five test results. */
static void
ok_article(int n, const char *path, const char *error, bool slow, bool shift)
{
    CHANNEL *cp;
    char *article, *wire, *body;
    size_t i, len, wirelen, offset;
    struct stat st;
    bool okay = true;
    enum channel_state expected;

    article = ReadInFile(path, &st);
    if (article == nullptr)
        sysbail("cannot read %s", path);
    len = st.st_size;
    wire = wire_from_native(article, len, &wirelen);
    if (wire == nullptr)
        sysbail("cannot convert %s to wire format", path);
    cp = fake_channel();
    offset = shift ? random() % 50 : 0;
    cp->Start = offset;
    cp->Next = offset;
    buffer_resize(&cp->In, wirelen + offset);
    memset(cp->In.data, '\0', offset);
    cp->In.used = offset;
    ARTprepare(cp);
    if (slow) {
        for (i = 0; i < wirelen; i++) {
            cp->In.data[i + offset] = wire[i];
            cp->In.used++;
            message_handlers_warn(0);
            ARTparse(cp);
            message_handlers_warn(1, message_log_stderr);
            if (i < wirelen - 1 && cp->State != CSeatarticle)
                if (cp->State != CSgetheader && cp->State != CSgetbody) {
                    okay = false;
                    warn("Bad state %u at %ld", cp->State, (long) i);
                    break;
                }
        }
    } else {
        buffer_append(&cp->In, wire, wirelen);
        cp->In.used = offset + wirelen;
        message_handlers_warn(0);
        ARTparse(cp);
        message_handlers_warn(1, message_log_stderr);
    }
    ok(n++, okay);
    if (wirelen > innconf->maxartsize)
        expected = CSgotlargearticle;
    else if (wirelen == 5)
        expected = CSnoarticle;
    else
        expected = CSgotarticle;
    ok_int(n++, expected, cp->State);
    ok_int(n++, wirelen, cp->Next - cp->Start);
    body = wire_findbody(wire, wirelen);
    if (body != nullptr)
        ok_int(n++, body - wire, cp->Data.Body - offset);
    else
        ok_int(n++, cp->Start, cp->Data.Body);
    ok_string(n++, error, cp->Error);
    free(article);
    free(wire);
    free(cp);
}

/*
**  The body scan (ARTparsebody): its counters, the NUL check and resuming
**  across reads.  The articles above come from native files through
**  wire_from_native, which cannot make a bare CR or LF, and the checks
**  above do not look at the counters, so these cases are built as wire
**  data here.  They pin down the current behaviour, quirks included, so a
**  faster scan must give exactly the same results.
*/

/* Every article below has this header, which adds nothing to the
   counters; the body starts right after it. */
static const char body_header[] = "Subject: x\r\n\r\n";

/* What a parse of one article left in the channel. */
struct scan_result {
    int state;
    size_t next;   /* cp->Next - cp->Start */
    size_t body;   /* cp->Data.Body - cp->Start */
    int lines;
    int lfwithoutcr;
    int crwithoutlf;
    int dotstuffed;
    int headerlines;
    std::string fields; /* every header field ARTcheckheader recorded */
    std::string error;

    bool operator==(const scan_result &) const = default;
};

/* Parse wire, given in pieces that end at each offset in cuts (the last
   piece ends at the end), calling ARTparse after each piece as innd does
   after each read. */
static scan_result
scan(const std::string &wire, const std::vector<size_t> &cuts)
{
    CHANNEL *cp = fake_channel();
    scan_result r;
    size_t done = 0;

    buffer_resize(&cp->In, wire.size());
    cp->In.used = 0;
    ARTprepare(cp);
    std::vector<size_t> ends(cuts);
    ends.push_back(wire.size());
    for (size_t end : ends) {
        if (end <= done)
            continue;
        memcpy(cp->In.data + done, wire.data() + done, end - done);
        cp->In.used = end;
        done = end;
        message_handlers_warn(0);
        ARTparse(cp);
        message_handlers_warn(1, message_log_stderr);
    }
    r.state = cp->State;
    r.next = cp->Next - cp->Start;
    r.body = cp->Data.Body - cp->Start;
    r.lines = cp->Data.Lines;
    r.lfwithoutcr = cp->Data.LFwithoutCR;
    r.crwithoutlf = cp->Data.CRwithoutLF;
    r.dotstuffed = cp->Data.DotStuffedLines;
    r.headerlines = cp->Data.HeaderLines;
    for (int h = 0; h < MAX_ARTHEADER; h++) {
        const HDRCONTENT &hc = cp->Data.HdrContent[h];

        if (hc.duplicated())
            r.fields += std::string(ARTheaders[h].Name) + " (duplicated)\n";
        else if (hc.found())
            r.fields += std::string(ARTheaders[h].Name) + "=["
                        + std::string(hc.view()) + "]\n";
    }
    r.error = cp->Error;
    free(cp->In.data);
    free(cp);
    return r;
}

static scan_result
scan_whole(const std::string &wire)
{
    return scan(wire, {});
}

static scan_result
scan_bytewise(const std::string &wire)
{
    std::vector<size_t> cuts;
    for (size_t i = 1; i < wire.size(); i++)
        cuts.push_back(i);
    return scan(wire, cuts);
}

/* Whether splitting wire in two at every offset gives the whole parse. */
static bool
scan_splits_agree(const std::string &wire, const scan_result &whole)
{
    for (size_t i = 1; i < wire.size(); i++)
        if (!(scan(wire, {i}) == whole)) {
            diag("split at %lu differs", (unsigned long) i);
            return false;
        }
    return true;
}

/* A plain restatement of the current body scan, for the random bodies:
   the counters a complete article gives, and where the scan stops.  The
   first body line is never counted as dot-stuffed: the header scan counts
   dots only after the header lines' CRLFs, and the body scan only after
   body CRLFs (CONVERSION_NOTES.md, "Upstream bugs and quirks found"). */
/* What body_header alone records (its header line and Subject field), for
   the body cases' expected results; set by test_body_scan. */
static scan_result body_header_result;

static scan_result
reference_body(const std::string &wire, size_t body, scan_result r)
{
    const char *d = wire.data();
    size_t i;

    r.state = CSgotarticle;
    r.body = body;
    for (i = body; i < wire.size(); i++) {
        if (d[i] == '\0')
            r.error = "437 Nul character in body";
        if (d[i] == '\n')
            r.lfwithoutcr++;
        if (d[i] != '\r')
            continue;
        if (i == body + 1 && wire.compare(i - 1, 3, ".\r\n") == 0) {
            r.next = i + 2;
            return r;
        }
        if (wire.compare(i, 5, "\r\n.\r\n") == 0) {
            r.lines++;
            r.next = i + 5;
            return r;
        }
        if (i + 1 < wire.size() && d[i + 1] == '\n') {
            i++;
            r.lines++;
            if (i + 1 < wire.size() && d[i + 1] == '.')
                r.dotstuffed++;
        } else {
            r.crwithoutlf++;
        }
    }
    r.next = i;
    return r;
}

static scan_result
reference_scan(const std::string &wire)
{
    scan_result r{};

    r.headerlines = body_header_result.headerlines;
    r.fields = body_header_result.fields;
    return reference_body(wire, sizeof(body_header) - 1, r);
}

/* Named body cases: the body, ending in CRLF (the terminating ".\r\n" is
   added), and the counters it must give.  Lines counts the CRLFs in the
   body. */
static const struct {
    const char *name;
    std::string body;
    int lines, lfwithoutcr, crwithoutlf, dotstuffed;
    const char *error;
} body_cases[] = {
    {"one line",             "a\r\n",            1, 0, 0, 0, ""},
    {"two lines",            "a\r\nb\r\n",       2, 0, 0, 0, ""},
    {"empty lines",          "\r\n\r\n",         2, 0, 0, 0, ""},
    {"dot-stuffed line",     "a\r\n..b\r\n",     2, 0, 0, 1, ""},
    {"dot-stuffed first",    "..a\r\n",          1, 0, 0, 0, ""},
    {"dot then text",        "a\r\n.x\r\n",      2, 0, 0, 1, ""},
    {"bare LF",              "a\nb\r\n",         1, 1, 0, 0, ""},
    {"bare LF before end",   "a\n\r\n",          1, 1, 0, 0, ""},
    {"bare CR",              "a\rb\r\n",         1, 0, 1, 0, ""},
    {"CR CR LF",             "a\r\r\n",          1, 0, 1, 0, ""},
    {"NUL",                  std::string("a\0b\r\n", 5),
                                                 1, 0, 0, 0,
     "437 Nul character in body"},
};

/* Seven checks for one way of feeding a named case. */
static int
ok_scan(int n, const scan_result &got, const scan_result &want,
        const char *name, const char *how)
{
    ok(n++, got.state == want.state);
    ok(n++, got.next == want.next);
    ok_int(n++, want.lines, got.lines);
    ok_int(n++, want.lfwithoutcr, got.lfwithoutcr);
    ok_int(n++, want.crwithoutlf, got.crwithoutlf);
    ok_int(n++, want.dotstuffed, got.dotstuffed);
    ok_string(n++, want.error.c_str(), got.error.c_str());
    if (!(got == want))
        diag("body case \"%s\", %s", name, how);
    return n;
}

/* A small fixed-seed generator, so failures repeat. */
static uint32_t
next_random(uint32_t &state)
{
    state = state * 1103515245u + 12345u;
    return state >> 8;
}

/* A random body over the bytes the scan cares about, as a complete
   article whose only terminator is the one at its end. */
static std::string
random_article(uint32_t &state)
{
    static const char alphabet[] = {'a', 'b', '.', '.', '\r', '\r', '\n',
                                    '\n', ' ', '\0'};
    for (;;) {
        std::string wire(body_header);
        size_t len = next_random(state) % 200;
        for (size_t i = 0; i < len; i++)
            wire += alphabet[next_random(state) % sizeof(alphabet)];
        wire += "\r\n.\r\n";
        if (reference_scan(wire).next == wire.size())
            return wire;
    }
}

/* Articles over maxartsize (8192 here) end in CSgotlargearticle with the
   size error, however they are split.  Their counters are not compared:
   once the channel is eating an article (CSeatarticle), the body scan runs
   from wherever the limit was crossed, even inside the header, so they
   depend on the split, and nothing uses them for a rejected article.
   Three checks. */
static int
ok_large(int n, const std::string &wire, const char *name)
{
    char error[128];
    bool agree = true;

    snprintf(error, sizeof(error),
             "437 Article of %lu bytes exceeds local limit of %lu bytes",
             (unsigned long) wire.size(), innconf->maxartsize);
    scan_result whole = scan_whole(wire);
    ok(n++, whole.state == CSgotlargearticle && whole.next == wire.size());
    ok_string(n++, error, whole.error.c_str());
    scan_result r = scan_bytewise(wire);
    if (r.state != whole.state || r.next != whole.next || r.error != whole.error)
        agree = false;
    for (size_t i = 1; agree && i < wire.size(); i++) {
        r = scan(wire, {i});
        if (r.state != whole.state || r.next != whole.next
            || r.error != whole.error) {
            diag("split at %lu differs", (unsigned long) i);
            agree = false;
        }
    }
    ok(n++, agree);
    if (!agree)
        diag("large article \"%s\"", name);
    return n;
}

enum { BODY_RANDOM = 300 };

static int
test_body_scan(int n)
{
    body_header_result = scan_whole(std::string(body_header) + ".\r\n");
    for (const auto &c : body_cases) {
        std::string wire = std::string(body_header) + c.body + ".\r\n";
        scan_result want{};
        want.state = CSgotarticle;
        want.next = wire.size();
        want.body = sizeof(body_header) - 1;
        want.lines = c.lines;
        want.lfwithoutcr = c.lfwithoutcr;
        want.crwithoutlf = c.crwithoutlf;
        want.dotstuffed = c.dotstuffed;
        want.error = c.error;
        want.headerlines = body_header_result.headerlines;
        want.fields = body_header_result.fields;
        scan_result whole = scan_whole(wire);
        n = ok_scan(n, whole, want, c.name, "whole");
        n = ok_scan(n, scan_bytewise(wire), want, c.name, "byte by byte");
        ok(n++, scan_splits_agree(wire, whole));
    }

    /* The terminator at the very start of the body. */
    {
        std::string wire = std::string(body_header) + ".\r\n";
        scan_result want{};
        want.state = CSgotarticle;
        want.next = wire.size();
        want.body = sizeof(body_header) - 1;
        want.headerlines = body_header_result.headerlines;
        want.fields = body_header_result.fields;
        n = ok_scan(n, scan_whole(wire), want, "empty body", "whole");
        n = ok_scan(n, scan_bytewise(wire), want, "empty body", "byte by byte");
    }

    /* Over the limit in the body: the rest is scanned while eating.  Over
       it in the header, then an empty body: split after the blank line,
       the body starts with the terminator while eating. */
    {
        std::string body;
        for (int i = 0; i < 3000; i++)
            body += "ab\r\n";
        n = ok_large(n, std::string(body_header) + body + ".\r\n",
                     "large body");
        n = ok_large(n, "Subject: " + std::string(9000, 'x') + "\r\n\r\n.\r\n",
                     "large header, empty body");
    }

    uint32_t state = 0x1e57;
    for (int i = 0; i < BODY_RANDOM; i++) {
        std::string wire = random_article(state);
        scan_result want = reference_scan(wire);
        scan_result whole = scan_whole(wire);
        ok(n++, whole == want);
        ok(n++, scan_bytewise(wire) == want);
        ok(n++, scan_splits_agree(wire, want));
        if (!(whole == want))
            diag("random body %d differs from the reference", i);
    }
    return n;
}

/*
**  Header scan.  The same approach as the body scan: named cases with
**  results worked out by hand, and random headers that must parse the same
**  whole, byte by byte and split anywhere, with the counters a plain
**  restatement of the scan gives.  ARTcheckheader's work (the fields it
**  records and the errors it reports) is compared between the ways of
**  feeding, and against hand-worked results in the named cases.
**
**  Two quirks are pinned down, not endorsed.  After the CRLF that ends a
**  header line, the scan steps past the first octet of the next line
**  without looking at it unless it is a CR, so a bare LF there is not
**  counted, and a NUL there is not reported (an upstream bug: such an
**  article is accepted; CONVERSION_NOTES.md, "Upstream bugs and quirks
**  found").  And the first body line is never counted as dot-stuffed (the
**  body scan's quirk, above).
*/

/* A plain restatement of the current header scan, for random articles. */
static scan_result
reference_header_scan(const std::string &wire)
{
    scan_result r{};
    const char *d = wire.data();
    const size_t size = wire.size();
    size_t i;

    r.state = CSgetheader;
    for (i = 0; i < size; i++) {
        if (d[i] == '\n')
            r.lfwithoutcr++;
        if (d[i] != '\r')
            continue;
        if (i == 1 && size - i > 1 && wire.compare(0, 3, ".\r\n") == 0) {
            r.state = CSnoarticle;
            r.next = i + 2;
            return r;
        }
        if (size - i < 5) {
            r.next = i;
            return r;
        }
        if (wire.compare(i, 5, "\r\n.\r\n") == 0) {
            r.state = i == 0 ? CSnoarticle : CSgotarticle;
            r.next = i + 5;
            return r;
        }
        if (d[i + 1] == '\n') {
            if (i != 0) {
                i += 2;
                r.headerlines++;
                if (d[i] == '.')
                    r.dotstuffed++;
            }
            if (d[i] == '\r' && d[i + 1] == '\n')
                return reference_body(wire, i + 2, r);
        } else {
            r.crwithoutlf++;
        }
    }
    r.next = i;
    return r;
}

/* Whether two results agree on what the header and body scans themselves
   decide (not ARTcheckheader's fields and errors). */
static bool
same_counters(const scan_result &a, const scan_result &b)
{
    return a.state == b.state && a.next == b.next && a.body == b.body
           && a.lines == b.lines && a.lfwithoutcr == b.lfwithoutcr
           && a.crwithoutlf == b.crwithoutlf && a.dotstuffed == b.dotstuffed
           && a.headerlines == b.headerlines;
}

/* Named header cases: the whole article and everything it must give.
   Fields lists ARTcheckheader's records as "Name=[value]" lines, in
   ARTheaders order. */
static const struct {
    const char *name;
    std::string wire;
    int state;
    size_t body;
    int headerlines, lines, lfwithoutcr, crwithoutlf, dotstuffed;
    std::string fields;
    const char *error;
} header_cases[] = {
    {"two fields", "Subject: x\r\nFrom: y\r\n\r\nb\r\n.\r\n", CSgotarticle,
     23, 2, 1, 0, 0, 0, "From=[y]\nSubject=[x]\n", ""},
    {"continuation", "Subject: x\r\n y\r\n\r\nb\r\n.\r\n", CSgotarticle,
     18, 2, 1, 0, 0, 0, "Subject=[x\r\n y]\n", ""},
    {"dot-started line", "Subject: x\r\n.A: b\r\n\r\nb\r\n.\r\n",
     CSgotarticle, 21, 2, 1, 0, 0, 1, "Subject=[x]\n", ""},
    {"bare LF", "Subject: x\ny\r\n\r\nb\r\n.\r\n", CSgotarticle, 16, 1, 1,
     1, 0, 0, "Subject=[x\ny]\n", ""},
    {"bare CR", "Subject: x\ry\r\n\r\nb\r\n.\r\n", CSgotarticle, 16, 1, 1,
     0, 1, 0, "Subject=[x\ry]\n", ""},
    {"CR CR LF", "Subject: x\r\r\n\r\nb\r\n.\r\n", CSgotarticle, 15, 1, 1,
     0, 1, 0, "Subject=[x\r]\n", ""},
    {"NUL", std::string("Subject: x\0y\r\n\r\nb\r\n.\r\n", 22),
     CSgotarticle, 16, 1, 1, 0, 0, 0,
     std::string("Subject=[x\0y]\n", 14), "437 Nul character in header"},
    {"no headers", "\r\nb\r\n.\r\n", CSgotarticle, 2, 0, 1, 0, 0, 0, "",
     "437 No headers"},
    {"empty article", ".\r\n", CSnoarticle, 0, 0, 0, 0, 0, 0, "",
     "437 Empty article"},
    {"empty headers and body", "\r\n.\r\n", CSnoarticle, 0, 0, 0, 0, 0, 0,
     "", "437 Empty headers and body"},
    {"no body", "Subject: x\r\n.\r\n", CSgotarticle, 0, 0, 0, 0, 0, 0, "",
     "437 No body"},
    {"empty body", "Subject: x\r\n\r\n.\r\n", CSgotarticle, 14, 1, 0, 0, 0,
     0, "Subject=[x]\n", ""},
    {"body starts with dot", "Subject: x\r\n\r\n..b\r\n.\r\n", CSgotarticle,
     14, 1, 1, 0, 0, 0, "Subject=[x]\n", ""},
    {"duplicated Message-ID",
     "Message-ID: <a@b>\r\nMessage-ID: <c@d>\r\n\r\nb\r\n.\r\n",
     CSgotarticle, 40, 2, 1, 0, 0, 0, "Message-ID (duplicated)\n", ""},
    {"Message-ID trimmed", "Message-ID:   <a@b>  \r\n\r\nb\r\n.\r\n",
     CSgotarticle, 25, 1, 1, 0, 0, 0, "Message-ID=[<a@b>]\n", ""},
    /* The quirk: the first octet of a header line is skipped. */
    {"LF first on a line", "Subject: x\r\n\nA: b\r\n\r\nb\r\n.\r\n",
     CSgotarticle, 21, 2, 1, 0, 0, 0, "Subject=[x]\n", ""},
    {"NUL first on a line",
     std::string("Subject: x\r\n\0A: b\r\n\r\nb\r\n.\r\n", 27), CSgotarticle,
     21, 2, 1, 0, 0, 0, "Subject=[x]\n", ""},
};

/* Nine checks for one way of feeding a named header case. */
static int
ok_hscan(int n, const scan_result &got, const scan_result &want,
         const char *name, const char *how)
{
    n = ok_scan(n, got, want, name, how);
    ok(n++, got.body == want.body && got.headerlines == want.headerlines);
    ok(n++, got.fields == want.fields);
    if (!(got == want))
        diag("header case \"%s\", %s", name, how);
    return n;
}

/* A random article: header lines from pieces that include the octets the
   header scan cares about, a blank line, a short body and the terminator,
   with that terminator its only one. */
static std::string
random_header_article(uint32_t &state)
{
    static const std::string pieces[] = {
        "Subject: x", "From: y", "Message-ID: <a@b>", "Path: p", "A: b",
        " c", "\r\n", "\r\n", "\r\n", "\r", "\n", ".", ".",
        std::string(1, '\0'), "a", ":"};
    for (;;) {
        std::string wire;
        size_t len = next_random(state) % 30;
        for (size_t i = 0; i < len; i++)
            wire += pieces[next_random(state) % ARRAY_SIZE(pieces)];
        wire += "\r\n\r\n";
        len = next_random(state) % 10;
        for (size_t i = 0; i < len; i++)
            wire += "ab.\r\n"[next_random(state) % 5];
        wire += "\r\n.\r\n";
        scan_result r = reference_header_scan(wire);
        if (r.next == wire.size()
            && (r.state == CSgotarticle || r.state == CSnoarticle))
            return wire;
    }
}

enum { HEADER_RANDOM = 300 };

static int
test_header_scan(int n)
{
    for (const auto &c : header_cases) {
        scan_result want{};
        want.state = c.state;
        want.next = c.wire.size();
        want.body = c.body;
        want.headerlines = c.headerlines;
        want.lines = c.lines;
        want.lfwithoutcr = c.lfwithoutcr;
        want.crwithoutlf = c.crwithoutlf;
        want.dotstuffed = c.dotstuffed;
        want.fields = c.fields;
        want.error = c.error;
        scan_result whole = scan_whole(c.wire);
        n = ok_hscan(n, whole, want, c.name, "whole");
        n = ok_hscan(n, scan_bytewise(c.wire), want, c.name, "byte by byte");
        ok(n++, scan_splits_agree(c.wire, whole));
    }

    uint32_t state = 0x4ead;
    for (int i = 0; i < HEADER_RANDOM; i++) {
        std::string wire = random_header_article(state);
        scan_result want = reference_header_scan(wire);
        scan_result whole = scan_whole(wire);
        ok(n++, same_counters(whole, want));
        ok(n++, scan_bytewise(wire) == whole);
        ok(n++, scan_splits_agree(wire, whole));
        if (!same_counters(whole, want))
            diag("random header %d differs from the reference", i);
    }
    return n;
}

int
main(void)
{
    size_t i;
    int n = 1;

    test_init(ARRAY_SIZE(articles) * 5 * 4 + ARRAY_SIZE(body_cases) * 15
              + 14 + 6 + BODY_RANDOM * 3 + ARRAY_SIZE(header_cases) * 19
              + HEADER_RANDOM * 3);
    initialize();
    message_handlers_notice(0);

    for (i = 0; i < ARRAY_SIZE(articles); i++) {
        ok_article(n, articles[i].path, articles[i].error, false, false);
        n += 5;
        ok_article(n, articles[i].path, articles[i].error, true, false);
        n += 5;
        ok_article(n, articles[i].path, articles[i].error, false, true);
        n += 5;
        ok_article(n, articles[i].path, articles[i].error, true, true);
        n += 5;
    }
    n = test_body_scan(n);
    test_header_scan(n);
    return 0;
}
