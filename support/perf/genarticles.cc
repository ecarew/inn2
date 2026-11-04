// genarticles: write a reproducible set of Usenet articles for the
// performance comparison (doc/spec/performance-comparison.md, section 4.1).
//
//   genarticles <count> <seed> <date> > articles.feed
//
// <date> is the Date header (RFC 5322 form) for all articles, so a run can
// keep them within innd's artcutoff; for a fixed seed, count and date the
// output is the same on every platform and compiler.  std::mt19937's
// output is fixed by the C++ standard, but the std:: distributions are
// not (libc++ and libstdc++ differ), so every random choice is made here
// from the engine's raw 32-bit output.
//
// Output, one record per article:
//   #! <message-id> <groups-count>\r\n
//   the article in NNTP wire form: CRLF lines, dot-stuffed, ending ".\r\n"
// support/perf/feeder reads it and sends each article with TAKETHIS or
// CHECK.  Groups are perf.h<1..5>.g<01..10>; perf-active (written with
// -a) lists them for the scratch server's active file.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kHierarchies = 5;
constexpr int kGroupsPer = 10;

class Rng {
public:
    explicit Rng(std::uint32_t seed) : engine_(seed) {}

    // Uniform in [0, n), by rejection, so no modulo bias.
    std::uint32_t below(std::uint32_t n)
    {
        const std::uint32_t limit = UINT32_MAX - UINT32_MAX % n;
        std::uint32_t v;
        do {
            v = static_cast<std::uint32_t>(engine_());
        } while (v >= limit);
        return v % n;
    }

    // Uniform in [lo, hi].
    std::uint32_t between(std::uint32_t lo, std::uint32_t hi)
    {
        return lo + below(hi - lo + 1);
    }

    // True with probability percent/100.
    bool chance(std::uint32_t percent) { return below(100) < percent; }

private:
    std::mt19937 engine_;
};

constexpr std::string_view kWords[] = {
    "the", "server", "article", "feed", "news", "group", "peer", "spool",
    "history", "overview", "reader", "post", "thread", "reply", "message",
    "header", "body", "network", "storage", "expire", "batch", "stream",
    "protocol", "client", "transit", "hierarchy", "moderated", "archive",
    "control", "cancel", "path", "date", "subject", "summary", "question",
    "answer", "problem", "solution", "configuration", "release", "version",
    "build", "compile", "install", "test", "result", "performance", "memory",
    "disk", "file", "line", "word", "and", "of", "to", "in", "is", "that",
    "it", "for", "on", "with", "as", "was", "this", "but", "be", "at", "by",
    "not", "from", "or", "have", "an", "they", "which", "you", "were", "all",
};
constexpr std::string_view kAgents[] = {
    "tin/2.6.4-20240224 (UNIX) (Linux/6.12.0 (x86_64))",
    "Mozilla Thunderbird",
    "slrn/1.0.3 (Linux)",
    "Gnus/5.13 (Gnus v5.13)",
    "knews 1.0b.1",
};
constexpr std::string_view kHosts[] = {
    "hub1.example", "hub2.example", "relay.example", "feed.example",
    "news.example", "transit.example", "peer.example", "edge.example",
};

std::string group_name(int h, int g)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "perf.h%d.g%02d", h + 1, g + 1);
    return buf;
}

std::string message_id(long n, std::uint32_t seed)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "<perf.%ld.%08x@poster.example>", n,
                  static_cast<unsigned>(seed));
    return buf;
}

// Article size in bytes of body: most 1-4 KB, some to 16 KB, a few to
// 64 KB.
std::uint32_t body_size(Rng &rng)
{
    const std::uint32_t r = rng.below(100);
    if (r < 85)
        return rng.between(1024, 4096);
    if (r < 97)
        return rng.between(4097, 16384);
    return rng.between(16385, 65536);
}

void append_body(Rng &rng, std::string &out, std::uint32_t size)
{
    std::string line;
    std::size_t written = 0;
    while (written < size) {
        const std::string_view w =
            kWords[rng.below(std::size(kWords))];
        if (!line.empty() && line.size() + 1 + w.size() > 72) {
            out += line;
            out += "\r\n";
            written += line.size() + 2;
            line.clear();
        }
        if (!line.empty())
            line += ' ';
        line += w;
    }
    if (!line.empty()) {
        out += line;
        out += "\r\n";
    }
}

// Dot-stuff and terminate an article whose lines all end in CRLF.
void put_article(std::FILE *f, const std::string &art)
{
    std::size_t pos = 0;
    while (pos < art.size()) {
        if (art[pos] == '.')
            std::fputc('.', f);
        const std::size_t end = art.find("\r\n", pos);
        std::fwrite(art.data() + pos, 1, end + 2 - pos, f);
        pos = end + 2;
    }
    std::fputs(".\r\n", f);
}

int usage()
{
    std::fputs("usage: genarticles <count> <seed> <date>\n"
               "       genarticles -a   (active file lines)\n",
               stderr);
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "-a") {
        for (int h = 0; h < kHierarchies; ++h)
            for (int g = 0; g < kGroupsPer; ++g)
                std::printf("%s 0000000000 0000000001 y\n",
                            group_name(h, g).c_str());
        return 0;
    }
    if (argc != 4)
        return usage();
    char *end;
    const long count = std::strtol(argv[1], &end, 10);
    if (*end != '\0' || count <= 0)
        return usage();
    const unsigned long seed_arg = std::strtoul(argv[2], &end, 0);
    if (*end != '\0')
        return usage();
    const auto seed = static_cast<std::uint32_t>(seed_arg);
    const std::string date = argv[3];

    Rng rng(seed);
    std::vector<std::string> ids;
    ids.reserve(static_cast<std::size_t>(count));
    std::string art;
    for (long n = 1; n <= count; ++n) {
        const std::string id = message_id(n, seed);

        // Newsgroups: one group, or 2-4 distinct ones for about 15%.
        const int ngroups = rng.chance(15)
                                ? static_cast<int>(rng.between(2, 4))
                                : 1;
        std::vector<std::string> groups;
        while (static_cast<int>(groups.size()) < ngroups) {
            std::string g = group_name(
                static_cast<int>(rng.below(kHierarchies)),
                static_cast<int>(rng.below(kGroupsPer)));
            bool dup = false;
            for (const auto &have : groups)
                dup = dup || have == g;
            if (!dup)
                groups.push_back(std::move(g));
        }

        art.clear();
        art += "Path: ";
        const std::uint32_t hops = rng.between(1, 4);
        for (std::uint32_t i = 0; i < hops; ++i) {
            art += kHosts[rng.below(std::size(kHosts))];
            art += '!';
        }
        art += "poster.example!not-for-mail\r\n";
        art += "From: Poster " + std::to_string(rng.below(5000)) +
               " <user" + std::to_string(rng.below(5000)) +
               "@poster.example>\r\n";
        art += "Newsgroups: ";
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (i > 0)
                art += ',';
            art += groups[i];
        }
        art += "\r\n";

        // About 40% are replies, with 1-5 References to earlier articles.
        const bool reply = n > 1 && rng.chance(40);
        art += "Subject: ";
        if (reply)
            art += "Re: ";
        for (std::uint32_t i = 0, w = rng.between(2, 8); i < w; ++i) {
            if (i > 0)
                art += ' ';
            art += kWords[rng.below(std::size(kWords))];
        }
        art += "\r\nMessage-ID: " + id + "\r\nDate: " + date + "\r\n";
        if (reply) {
            art += "References:";
            for (std::uint32_t i = 0, r = rng.between(1, 5); i < r; ++i) {
                art += ' ';
                art += ids[rng.below(static_cast<std::uint32_t>(ids.size()))];
            }
            art += "\r\n";
        }
        art += "User-Agent: ";
        art += kAgents[rng.below(std::size(kAgents))];
        art += "\r\nOrganization: Performance Comparison\r\n\r\n";
        append_body(rng, art, body_size(rng));

        std::printf("#! %s %zu\r\n", id.c_str(), groups.size());
        put_article(stdout, art);
        ids.push_back(id);
    }
    return std::fflush(stdout) == 0 ? 0 : 1;
}
