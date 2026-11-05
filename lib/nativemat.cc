/*
**  nativemat: wildmat pattern matching with Unicode UTF-8 extensions.
**
**  Shell-style matching of ?, \, [] and * against octet strings, with
**  comma-separated lists, ! to negate a pattern in a list and, for
**  nativemat_poison, @ to poison.  It is 8-bit clean: ? and * step over whole
**  UTF-8 characters, and an octet that does not start a valid UTF-8
**  sequence counts as one character.  A malformed pattern (an unterminated
**  character class or a trailing backslash) matches nothing.
**
**  This replaces lib/uwildmat.c, whose history it inherits: original by Rich
**  $alz, 1986; ABORT code by Lars Mathiesen, 1991, which ensured only one
**  star loop is ever active; minus and close-bracket handling in character
**  classes by Robert Elz, 1991; ',', '!' and '@' lists and UTF-8 support by
**  Russ Allbery, 2000; fixes and improvements by many since.
**
**  November 2026 (INN++): rewritten as an iterative matcher on
**  std::string_view.  It keeps the property the ABORT code gave the
**  recursive version, retrying only the most recent star, so the work is
**  bounded by text length times pattern length, with constant stack depth,
**  no allocation and no exceptions (not even std::string_view::substr's
**  range check, so C programs linking libinn need no C++ runtime).  It was checked against uwildmat on the
**  cases in support/perf/wildmat and on 1,000,000 random field-shaped cases
**  (CONVERSION_NOTES.md, uwildmat hardening).  The one difference is a fix:
**  in a list, an escaped \[ no longer starts a character class that
**  swallows the rest of the list.
**
**  For more information on UTF-8, see RFC 3629.
*/

#include "portable/system.h"

#include <bit>
#include <cstdint>
#include <string_view>

#include "inn/libinn.h"

namespace {

/*
**  The number of octets in the character at s[i]: a valid UTF-8 sequence of
**  2 to 6 octets, by the count of high one bits in its lead octet, or else 1,
**  so that malformed UTF-8 is treated as eight-bit local characters.
*/
constexpr size_t
char_length(std::string_view s, size_t i) noexcept
{
    auto lead = static_cast<unsigned char>(s[i]);
    auto length = static_cast<size_t>(std::countl_one(lead));

    if (length < 2 || length > 6 || i + length > s.size())
        return 1;
    for (size_t k = 1; k < length; k++)
        if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80)
            return 1;
    return length;
}


/*
**  The character at s[i] as a UCS-4 value, setting length to its length in
**  octets.  A malformed character is the value of its first octet; this is
**  wrong, but does the right thing for non-UTF-8 text.
*/
constexpr uint32_t
decode(std::string_view s, size_t i, size_t &length) noexcept
{
    auto lead = static_cast<unsigned char>(s[i]);
    uint32_t value;

    length = char_length(s, i);
    if (length == 1)
        return lead;
    value = lead & ((1U << (7 - length)) - 1);
    for (size_t k = 1; k < length; k++)
        value = (value << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3f);
    return value;
}


/*
**  Whether the character c is in the character class whose body (the text
**  between [ and ]) is given.  A leading ^ inverts the class.  A range may
**  start only after a member, so a leading - is a member and a-e-n is a
**  range followed by - and n.  The start of a range is also a member, so a
**  reversed range like m-a matches only m.
*/
constexpr bool
in_class(uint32_t c, std::string_view body) noexcept
{
    bool reversed = !body.empty() && body[0] == '^';
    bool allowrange = false;
    uint32_t first = 0;
    size_t i = 0;
    size_t length = 0;

    if (reversed)
        body.remove_prefix(1);
    while (i < body.size()) {
        if (allowrange && body[i] == '-' && i + 1 < body.size()) {
            uint32_t last = decode(body, i + 1, length);

            i += 1 + length;
            if (c >= first && c <= last)
                return !reversed;
            allowrange = false;
        } else {
            first = decode(body, i, length);
            i += length;
            if (c == first)
                return !reversed;
            allowrange = true;
        }
    }
    return reversed;
}


/*
**  Match text against a single pattern (no list syntax).  On a mismatch,
**  only the most recent star is retried, taking one more character; an
**  earlier star never needs to be, since anything the rest of the pattern
**  could match after it, it can also match after the most recent one.
*/
constexpr bool
match_pattern(std::string_view text, std::string_view pat) noexcept
{
    constexpr size_t none = std::string_view::npos;
    size_t t = 0;
    size_t p = 0;
    size_t star_p = none;
    size_t star_t = 0;

    for (;;) {
        bool failed = false;

        if (p == pat.size()) {
            if (t == text.size())
                return true;
            failed = true;
        } else if (pat[p] == '*') {
            while (p < pat.size() && pat[p] == '*')
                p++;
            if (p == pat.size())
                return true;
            star_p = p;
            star_t = t;
            continue;
        } else if (t == text.size()) {
            /* Pattern left but no text: a star taking more can't help. */
            return false;
        } else {
            switch (pat[p]) {
            case '\\':
                if (p + 1 == pat.size())
                    return false;
                p++;
                [[fallthrough]];
            default:
                if (text[t] != pat[p])
                    failed = true;
                else {
                    t++;
                    p++;
                }
                break;
            case '?':
                t += char_length(text, t);
                p++;
                break;
            case '[': {
                size_t start = p + 1;
                size_t length = 0;
                size_t close;

                /* A ] right after [ or [^ is a member, not the end. */
                close = start + (start < pat.size() && pat[start] == '^') + 1;
                if (close >= pat.size())
                    return false;
                close = pat.find(']', close);
                if (close == none)
                    return false;
                if (!in_class(decode(text, t, length),
                              std::string_view(pat.data() + start, close - start)))
                    failed = true;
                else {
                    t += length;
                    p = close + 1;
                }
                break;
            }
            }
        }
        if (!failed)
            continue;
        if (star_p == none || star_t >= text.size())
            return false;
        star_t += char_length(text, star_t);
        t = star_t;
        p = star_p;
    }
}


/*
**  Match text against a wildmat expression: a comma-separated list of
**  patterns, each optionally preceded by ! to invert it or, if allowpoison
**  is set, @ to poison it (as ! but returning NATIVEMAT_POISON if it is the
**  pattern that decides).  The last pattern that matches decides.
*/
enum nativemat
match_expression(std::string_view text, std::string_view expr,
                 bool allowpoison) noexcept
{
    bool match = false;
    bool poisoned = false;
    size_t p = 0;

    if (expr.empty())
        return text.empty() ? NATIVEMAT_MATCH : NATIVEMAT_FAIL;

    for (;;) {
        bool poison = allowpoison && p < expr.size() && expr[p] == '@';
        bool reverse = poison || (p < expr.size() && expr[p] == '!');
        bool escaped = false;
        size_t split;

        if (reverse)
            p++;

        /* Find the first comma that is neither escaped nor inside a
           character class. */
        for (split = p; split < expr.size(); split++) {
            if (expr[split] == '[' && !escaped) {
                split++;
                if (split < expr.size() && expr[split] == ']')
                    split++;
                while (split < expr.size() && expr[split] != ']')
                    split++;
                if (split == expr.size())
                    break;
            }
            if (expr[split] == ',' && !escaped)
                break;
            escaped = expr[split] == '\\' ? !escaped : false;
        }

        /* A pattern that can't change the result needn't be matched. */
        if ((match != !reverse || poison != poisoned)
            && match_pattern(text, std::string_view(expr.data() + p, split - p))) {
            poisoned = poison;
            match = !reverse;
        }
        if (split >= expr.size())
            break;
        p = split + 1;
    }
    if (poisoned)
        return NATIVEMAT_POISON;
    return match ? NATIVEMAT_MATCH : NATIVEMAT_FAIL;
}

} // namespace


/*
**  Match text against a comma list of patterns with ! negation; @ is an
**  ordinary character.
*/
bool
nativemat(const char *text, const char *pat)
{
    if (pat[0] == '*' && pat[1] == '\0')
        return true;
    return match_expression(text, pat, false) == NATIVEMAT_MATCH;
}


/*
**  As nativemat, but @ introduces a poison pattern.
*/
enum nativemat
nativemat_poison(const char *text, const char *pat)
{
    if (pat[0] == '*' && pat[1] == '\0')
        return NATIVEMAT_MATCH;
    return match_expression(text, pat, true);
}


/*
**  Match text against one pattern; , and ! are ordinary characters.
*/
bool
nativemat_simple(const char *text, const char *pat)
{
    if (pat[0] == '*' && pat[1] == '\0')
        return true;
    return match_pattern(text, pat);
}
