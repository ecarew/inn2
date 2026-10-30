/*
**  Benchmark: innd's header-name lookup, the balanced tree art.cc builds
**  today (ARTbuildtree, searched in ARTparseheader) against a sorted array
**  searched with std::lower_bound (CONVERSION_NOTES.md, "Article header
**  lookup").  Not part of INN; run with support/bench/header-lookup.sh.
**
**  Usage: header-lookup <names-file> <mix-file>
**  names-file: the 76 names of innd's ARTheaders[], one per line.
**  mix-file: header names to look up, one per line, in article order.
**
**  Both lookups copy the code they model: the tree is built and searched
**  exactly as art.cc does (including the first-letter shortcut), and the
**  array keeps that shortcut too.  Each prints the median time per lookup
**  over several rounds, and both must find the same entry for every name.
*/

#include <strings.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

struct Header {
    std::string name;
};

// The tree, as art.cc builds and searches it.
struct Tree {
    const char *Name;
    const Header *Hdr;
    Tree *Before;
    Tree *After;
};

Tree *
build_tree(const std::vector<const Header *> &table, int lo, int hi)
{
    const int mid = lo + (hi - lo) / 2;
    auto *tp = new Tree{table[mid]->name.c_str(), table[mid], nullptr, nullptr};
    if (mid != lo)
        tp->Before = build_tree(table, lo, mid);
    if (mid != hi - 1)
        tp->After = build_tree(table, mid + 1, hi);
    return tp;
}

const Header *
tree_find(const Tree *root, const char *header)
{
    const int c = islower((unsigned char) *header)
                      ? toupper((unsigned char) *header)
                      : *header;
    int i;
    const Tree *tp;
    for (tp = root; tp;) {
        if ((i = c - tp->Name[0]) == 0 && (i = strcasecmp(header, tp->Name)) == 0)
            break;
        tp = i < 0 ? tp->Before : tp->After;
    }
    return tp ? tp->Hdr : nullptr;
}

// The sorted array searched by std::lower_bound: always runs every step
// (it cannot stop at a match), then compares once more.
const Header *
array_find(const std::vector<const Header *> &sorted, const char *header)
{
    const int c = islower((unsigned char) *header)
                      ? toupper((unsigned char) *header)
                      : *header;
    auto it = std::lower_bound(sorted.begin(), sorted.end(), header,
                               [c](const Header *h, const char *name) {
                                   const int d = h->name[0] - c;
                                   if (d != 0)
                                       return d < 0;
                                   return strcasecmp(h->name.c_str(), name) < 0;
                               });
    if (it != sorted.end() && strcasecmp((*it)->name.c_str(), header) == 0)
        return *it;
    return nullptr;
}

// The sorted array searched as the tree is: the same probes in the same
// order (the middle of [lo, hi), as ARTbuildtree picks its nodes), the
// same three-way comparison and early exit, but contiguous entries with no
// pointers between them.
struct Entry {
    const char *Name;
    const Header *Hdr;
};

const Header *
bsearch_find(const std::vector<Entry> &table, const char *header)
{
    const int c = islower((unsigned char) *header)
                      ? toupper((unsigned char) *header)
                      : *header;
    size_t lo = 0, hi = table.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const Entry &e = table[mid];
        int i;
        if ((i = c - e.Name[0]) == 0 && (i = strcasecmp(header, e.Name)) == 0)
            return e.Hdr;
        if (i < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return nullptr;
}

// The tree laid out in an array in breadth-first (Eytzinger) order: entry
// i's children are 2i+1 and 2i+2, so the search follows tree links without
// storing them.
void
eytzinger(const std::vector<Entry> &sorted, std::vector<Entry> &out,
          size_t &next, size_t k)
{
    if (k >= sorted.size())
        return;
    eytzinger(sorted, out, next, 2 * k + 1);
    out[k] = sorted[next++];
    eytzinger(sorted, out, next, 2 * k + 2);
}

const Header *
eytzinger_find(const std::vector<Entry> &table, const char *header)
{
    const int c = islower((unsigned char) *header)
                      ? toupper((unsigned char) *header)
                      : *header;
    size_t k = 0;
    while (k < table.size()) {
        const Entry &e = table[k];
        int i;
        if ((i = c - e.Name[0]) == 0 && (i = strcasecmp(header, e.Name)) == 0)
            return e.Hdr;
        k = 2 * k + (i < 0 ? 1 : 2);
    }
    return nullptr;
}

// std::map: the standard library's balanced tree (a red-black tree in
// libc++ and libstdc++), with a case-insensitive ordering.
struct CaseLess {
    bool operator()(const char *a, const char *b) const
    {
        return strcasecmp(a, b) < 0;
    }
};
using HeaderMap = std::map<const char *, const Header *, CaseLess>;

const Header *
map_find(const HeaderMap &map, const char *header)
{
    auto it = map.find(header);
    return it == map.end() ? nullptr : it->second;
}

std::vector<std::string>
read_lines(const char *path)
{
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
        if (!line.empty())
            lines.push_back(line);
    return lines;
}

// Keep the compiler from proving a lookup's input or dropping its result,
// which otherwise lets it hoist the lookups out of the timing loop (the
// usual benchmark barrier, as in Google Benchmark's DoNotOptimize).
template <typename T>
inline void
opaque(T &value)
{
    asm volatile("" : "+r"(value) : : "memory");
}

template <typename F>
double
median_ns(F lookup, const std::vector<std::string> &mix, int reps)
{
    std::array<double, 15> rounds{};
    for (double &r : rounds) {
        const auto start = std::chrono::steady_clock::now();
        for (int n = 0; n < reps; n++)
            for (const auto &h : mix) {
                const char *name = h.c_str();
                opaque(name);
                const void *found = lookup(name);
                opaque(found);
            }
        const auto end = std::chrono::steady_clock::now();
        r = std::chrono::duration<double, std::nano>(end - start).count()
            / (double(reps) * double(mix.size()));
    }
    std::sort(rounds.begin(), rounds.end());
    return rounds[rounds.size() / 2];
}

} // namespace

int
main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s names-file mix-file\n", argv[0]);
        return 2;
    }
    std::vector<Header> headers;
    for (auto &n : read_lines(argv[1]))
        headers.push_back({n});
    const auto mix = read_lines(argv[2]);

    std::vector<const Header *> sorted;
    for (const auto &h : headers)
        sorted.push_back(&h);
    std::sort(sorted.begin(), sorted.end(), [](const Header *a, const Header *b) {
        return strcasecmp(a->name.c_str(), b->name.c_str()) < 0;
    });
    const Tree *root = build_tree(sorted, 0, int(sorted.size()));
    std::vector<Entry> entries;
    for (const Header *h : sorted)
        entries.push_back({h->name.c_str(), h});
    HeaderMap map;
    for (const Header *h : sorted)
        map.emplace(h->name.c_str(), h);
    std::vector<Entry> eyt(entries.size());
    size_t next = 0;
    eytzinger(entries, eyt, next, 0);

    // Both must agree on every name in the mix and on every table name.
    size_t known = 0;
    for (const auto *list : {&mix}) {
        for (const auto &h : *list) {
            const Header *a = tree_find(root, h.c_str());
            if (a != array_find(sorted, h.c_str())
                || a != bsearch_find(entries, h.c_str())
                || a != eytzinger_find(eyt, h.c_str())
                || a != map_find(map, h.c_str())) {
                std::fprintf(stderr, "disagree on %s\n", h.c_str());
                return 1;
            }
            known += a != nullptr;
        }
    }
    for (const auto &h : headers)
        if (tree_find(root, h.name.c_str()) != &h
            || array_find(sorted, h.name.c_str()) != &h
            || bsearch_find(entries, h.name.c_str()) != &h
            || eytzinger_find(eyt, h.name.c_str()) != &h) {
            std::fprintf(stderr, "not found: %s\n", h.name.c_str());
            return 1;
        }

    const int reps = 20000;
    const double t = median_ns([&](const char *n) { return tree_find(root, n); },
                               mix, reps);
    const double a = median_ns([&](const char *n) { return array_find(sorted, n); },
                               mix, reps);
    const double b = median_ns(
        [&](const char *n) { return bsearch_find(entries, n); }, mix, reps);
    const double e = median_ns(
        [&](const char *n) { return eytzinger_find(eyt, n); }, mix, reps);
    const double m = median_ns([&](const char *n) { return map_find(map, n); },
                               mix, reps);
    std::printf("%zu table names, %zu lookups per round (%zu known)\n",
                headers.size(), mix.size(), known);
    std::printf("per lookup, median of 15 rounds:\n");
    std::printf("tree (today):            %6.2f ns\n", t);
    std::printf("array, lower_bound:      %6.2f ns (%.2f x tree)\n", a, a / t);
    std::printf("array, tree's search:    %6.2f ns (%.2f x tree)\n", b, b / t);
    std::printf("array, Eytzinger order:  %6.2f ns (%.2f x tree)\n", e, e / t);
    std::printf("std::map:                %6.2f ns (%.2f x tree)\n", m, m / t);
    return 0;
}
