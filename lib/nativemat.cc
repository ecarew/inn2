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
**  November 2026 (INN++): rewritten as an iterative matcher, patterns as
**  std::string_view, texts as NUL-terminated strings that are never
**  measured.  It keeps the property the ABORT code gave the
**  recursive version, retrying only the most recent star, so the work is
**  bounded by text length times pattern length, with constant stack depth,
**  no allocation and no exceptions (not even std::string_view::substr's
**  range check, so C programs linking libinn need no C++ runtime).  It was
**  checked against uwildmat on the cases in support/perf/wildmat and on 1,000,000 random field-shaped cases
**  (CONVERSION_NOTES.md, uwildmat hardening).  The one difference is a fix:
**  in a list, an escaped \[ no longer starts a character class that
**  swallows the rest of the list.
**
**  is_valid_utf8, which moved here with the matcher, is unchanged.
**
**  For more information on UTF-8, see RFC 3629.
*/

#include "portable/system.h"

#include <bit>
#include <cctype>
#include <cstdint>
#include <string_view>

#include "inn/libinn.h"

/*
**  Check whether a string contains only valid UTF-8 characters, without
**  any ASCII control characters except for \r, \n and \t.
*/
bool
is_valid_utf8(const char *text)
{
    unsigned char mask;
    const unsigned char *p;
    int length;
    int left;

    for (p = (const unsigned char *) text; *p != '\0';) {
        mask = 0x80;
        length = 0;

        /* Find out the expected length of the character. */
        for (; mask > 0 && (*p & mask) == mask; mask >>= 1)
            length++;

        p++;

        /* Valid printable ASCII character or CR, LF or HTAB. */
        if (length == 0) {
            if (isprint((unsigned char) p[-1]) || p[-1] == '\r'
                || p[-1] == '\n' || p[-1] == '\t') {
                continue;
            } else {
                return false;
            }
        }

        /* Invalid length. */
        if (length < 2 || length > 6)
            return false;

        /* Check that each byte looks like 10xxxxxx, except for the first. */
        left = length - 1;
        for (; left > 0 && (*p & 0xc0) == 0x80; p++)
            left--;

        if (left > 0)
            return false;
    }

    return true;
}


namespace {

/*
**  The number of octets in the character at s, which has at most avail
**  octets left (or is NUL-terminated, with avail as large as it likes): a
**  valid UTF-8 sequence of 2 to 6 octets, by the count of high one bits in
**  its lead octet, or else 1, so that malformed UTF-8 is treated as eight-bit
**  local characters.  A NUL is never a continuation octet, so a
**  NUL-terminated string is never read past its end.
*/
constexpr size_t
char_length(const char *s, size_t avail) noexcept
{
    auto lead = static_cast<unsigned char>(s[0]);
    auto length = static_cast<size_t>(std::countl_one(lead));

    if (length < 2 || length > 6 || length > avail)
        return 1;
    for (size_t k = 1; k < length; k++)
        if ((static_cast<unsigned char>(s[k]) & 0xc0) != 0x80)
            return 1;
    return length;
}


/*
**  The character at s as a UCS-4 value, setting length to its length in
**  octets (avail as for char_length).  A malformed character is the value of
**  its first octet; this is wrong, but does the right thing for non-UTF-8
**  text.
*/
constexpr uint32_t
decode(const char *s, size_t avail, size_t &length) noexcept
{
    auto lead = static_cast<unsigned char>(s[0]);
    uint32_t value;

    length = char_length(s, avail);
    if (length == 1)
        return lead;
    value = lead & ((1U << (7 - length)) - 1);
    for (size_t k = 1; k < length; k++)
        value = (value << 6) | (static_cast<unsigned char>(s[k]) & 0x3f);
    return value;
}

/* For text, which is NUL-terminated and never measured. */
constexpr size_t unbounded = static_cast<size_t>(-1);


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
            uint32_t last =
                decode(body.data() + i + 1, body.size() - i - 1, length);

            i += 1 + length;
            if (c >= first && c <= last)
                return !reversed;
            allowrange = false;
        } else {
            first = decode(body.data() + i, body.size() - i, length);
            i += length;
            if (c == first)
                return !reversed;
            allowrange = true;
        }
    }
    return reversed;
}


/*
**  Match the NUL-terminated text against a single pattern (no list syntax).
**  The text is never measured: matching usually fails within a few octets,
**  and measuring every text for every pattern in a list was most of the
**  cost.  On a mismatch, only the most recent star is retried, taking one
**  more character; an earlier star never needs to be, since anything the
**  rest of the pattern could match after it, it can also match after the
**  most recent one.
**
**  When the star is followed by an ASCII literal, the star's next try jumps
**  straight to the next occurrence of that octet in the text (as uwildmat's
**  lookahead did).  This is safe for UTF-8: an ASCII octet never occurs
**  inside a multi-octet character, so it always starts a character.
*/
bool
match_pattern(const char *text, std::string_view pat) noexcept
{
    constexpr size_t none = std::string_view::npos;
    size_t t = 0;
    size_t p = 0;
    size_t star_p = none;
    size_t star_t = 0;

    /* Move star_t to where the pattern after the star can next match, or
       return false if it never can. */
    auto next_try = [&]() noexcept {
        unsigned char c = static_cast<unsigned char>(pat[star_p]);

        if (c == '\\' && star_p + 1 < pat.size())
            c = static_cast<unsigned char>(pat[star_p + 1]);
        else if (c == '?' || c == '[' || c == '\\')
            return true;
        if (c >= 0x80)
            return true;
        /* An inline loop, not strchr: the gaps are usually a few octets,
           where the call costs more than the scan. */
        while (text[star_t] != '\0' && text[star_t] != static_cast<char>(c))
            star_t++;
        return text[star_t] != '\0';
    };

    for (;;) {
        bool failed = false;

        if (p == pat.size()) {
            if (text[t] == '\0')
                return true;
            failed = true;
        } else if (pat[p] == '*') {
            while (p < pat.size() && pat[p] == '*')
                p++;
            if (p == pat.size())
                return true;
            star_p = p;
            star_t = t;
            if (!next_try())
                return false;
            t = star_t;
            continue;
        } else if (text[t] == '\0') {
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
                t += char_length(text + t, unbounded);
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
                if (!in_class(decode(text + t, unbounded, length),
                              std::string_view(pat.data() + start,
                                               close - start)))
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
        if (star_p == none || text[star_t] == '\0')
            return false;
        star_t += char_length(text + star_t, unbounded);
        if (!next_try())
            return false;
        t = star_t;
        p = star_p;
    }
}


/*
**  Match the NUL-terminated text against a NUL-terminated wildmat
**  expression: a comma-separated list of patterns, each optionally preceded
**  by ! to invert it or, if allowpoison is set, @ to poison it (as ! but
**  returning NATIVEMAT_POISON if it is the pattern that decides).  The last
**  pattern that matches decides.
*/
enum nativemat
match_expression(const char *text, const char *expr, bool allowpoison) noexcept
{
    bool match = false;
    bool poisoned = false;
    const char *p = expr;

    if (*expr == '\0')
        return *text == '\0' ? NATIVEMAT_MATCH : NATIVEMAT_FAIL;

    for (;;) {
        bool poison = allowpoison && *p == '@';
        bool reverse = poison || *p == '!';
        bool escaped = false;
        const char *split;

        if (reverse)
            p++;

        /* Find the first comma that is neither escaped nor inside a
           character class. */
        for (split = p; *split != '\0'; split++) {
            if (*split == '[' && !escaped) {
                split++;
                if (*split == ']')
                    split++;
                while (*split != '\0' && *split != ']')
                    split++;
                if (*split == '\0')
                    break;
            }
            if (*split == ',' && !escaped)
                break;
            escaped = *split == '\\' ? !escaped : false;
        }

        /* A pattern that can't change the result needn't be matched. */
        if ((match != !reverse || poison != poisoned)
            && match_pattern(text, std::string_view(
                                       p, static_cast<size_t>(split - p)))) {
            poisoned = poison;
            match = !reverse;
        }
        if (*split == '\0')
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
