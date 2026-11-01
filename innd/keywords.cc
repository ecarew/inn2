/*
**  Optional keyword generation code.
**
**  Additional code for sake of manufacturing Keywords header fields out of
**  air in order to provide better (scorable) OVER data, containing bits of
**  article body content which have a reasonable expectation of utility.
**
**  Basic idea: simple word-counting.  We find words in the article body,
**  separated by whitespace.  Remove punctuation.  Sort words, count unique
**  words, sort those counts.  Write the resulting Keywords header field
**  containing the poster's original Keywords header field (if any) followed
**  by a magic cookie separator and then the sorted list of words.
*/

#include "portable/system.h"

#include "inn/libinn.h"

#include "inn/innconf.h"
#include "innd.h"

#include "article.h"

#include <algorithm>

/*  If keyword support wasn't requested, stub out the main function provided by
**  this file.
*/
#if !DO_KEYWORDS
void
KEYgenerate(std::string &keywords, const char *body UNUSED,
            size_t bodylen UNUSED)
{
    keywords.clear();
}

#else

/*
**  For regex-based common word elimination.
*/
#    include <regex.h>

#    define MIN_WORD_LENGTH 3  /* 1- and 2-char words don't count. */
#    define MAX_WORD_LENGTH 28 /* Fits "antidisestablishmentarianism". */


/*
**  A trivial structure for keeping track of words via both
**  index to the overall word list and their counts.
*/
struct word_entry {
    int index;
    int length;
    int count;
};


/*
**  std::sort predicates for word_entry: decreasing frequency, then
**  decreasing word length; and for strings.
*/
static bool
wvec_freq_cmp(const struct word_entry &a, const struct word_entry &b)
{
    return a.count > b.count;
}

static bool
wvec_length_cmp(const struct word_entry &a, const struct word_entry &b)
{
    return a.length > b.length;
}

static bool
ptr_strcmp(const char *a, const char *b)
{
    return strcmp(a, b) < 0;
}


/*
**  Build new Keywords header field body.
*/

void
KEYgenerate(std::string &keywords, /* Generated body, replaced. */
            const char *body,      /* Article body. */
            size_t bodylen)        /* Article body length. */
{

    unsigned long word_count, word_index, word_length, distinct_words;
    int last;
    char *text, *text_end, *this_word, *chase, *punc;
    static struct word_entry *word_vec;
    static char **word;
    static const char *whitespace = " \t\r\n";

    /* Prototype setup: regex match preparation. */
    static int regex_lib_init = 0;
    static regex_t preg;
    static const char *elim_regexp =
        "^\\([-+/0-9][-+/"
        "0-9]*\\|.*1st\\|.*2nd\\|.*3rd\\|.*[04-9]th\\|about\\|after\\|ago\\|"
        "all\\|already\\|also\\|among\\|and\\|any\\|anybody\\|anyhow\\|"
        "anyone\\|anywhere\\|are\\|bad\\|because\\|been\\|before\\|being\\|"
        "between\\|but\\|can\\|could\\|did\\|does\\|doing\\|done\\|dont\\|"
        "during\\|eight\\|eighth\\|eleven\\|else\\|elsewhere\\|every\\|"
        "everywhere\\|few\\|five\\|fifth\\|first\\|for\\|four\\|fourth\\|"
        "from\\|get\\|going\\|gone\\|good\\|got\\|had\\|has\\|have\\|having\\|"
        "he\\|her\\|here\\|hers\\|herself\\|him\\|himself\\|his\\|how\\|ill\\|"
        "into\\|its\\|ive\\|just\\|kn[eo]w\\|least\\|less\\|let\\|like\\|"
        "look\\|many\\|may\\|more\\|m[ou]st\\|myself\\|next\\|nine\\|ninth\\|"
        "not\\|now\\|off\\|one\\|only\\|onto\\|our\\|out\\|over\\|really\\|"
        "said\\|saw\\|says\\|second\\|see\\|set\\|seven\\|seventh\\|several\\|"
        "shall\\|she\\|should\\|since\\|six\\|sixth\\|some\\|somehow\\|"
        "someone\\|something\\|somewhere\\|such\\|take\\|ten\\|tenth\\|than\\|"
        "that\\|the\\|their\\!|them\\|then\\|there\\|therell\\|theres\\|"
        "these\\|they\\|thing\\|things\\|third\\|this\\|those\\|three\\|"
        "thus\\|together\\|told\\|too\\|twelve\\|two\\|under\\|upon\\|very\\|"
        "via\\|want\\|wants\\|was\\|wasnt\\|way\\|were\\|weve\\|what\\|"
        "whatever\\|when\\|where\\|wherell\\|wheres\\|whether\\|which\\|"
        "while\\|who\\|why\\|will\\|with\\|would\\|write\\|writes\\|wrote\\|"
        "yes\\|yet\\|you\\|your\\|youre\\|yourself\\)$";

    if (word_vec == 0) {
        word_vec = xmalloc(innconf->keymaxwords * sizeof(struct word_entry));
        if (word_vec == 0)
            return;
        word = xmalloc(innconf->keymaxwords * sizeof(char *));
        if (word == NULL) {
            free(word_vec);
            return;
        }
    }

    if (regex_lib_init == 0) {
        regex_lib_init++;

        if (regcomp(&preg, elim_regexp, REG_ICASE | REG_NOSUB) != 0) {
            syslog(L_FATAL, "%s regcomp failure", LogName);
            abort();
        }
    }

    /* Start a fresh Keywords header field body, limited to the size
     * specified by the keylimit parameter in inn.conf. */
    keywords.clear();

    /* Now figure acceptable extents, and copy body to working string.
     * (Memory-intensive for hefty articles: limit to non-ABSURD articles.) */
    if ((bodylen < 100)
        || (bodylen > innconf->keyartlimit)) /* Too small/big to bother. */
        return;

    /* A NUL-terminated copy of the body, which the word scan cuts up. */
    std::string orig_text(body, bodylen);
    text = orig_text.data();

    text_end = text + bodylen;

    /* Abusive punctuation stripping: turn it all into spaces. */
    for (punc = text; *punc; punc++)
        if (!isalpha((unsigned char) *punc))
            *punc = ' ';

    /* Move to first word. */
    text += strspn(text, whitespace);
    word_count = 0;

    /* Hunt down words. */
    while ((text < text_end) && /* While there might be words... */
           (*text != '\0') && (word_count < innconf->keymaxwords)) {

        /* Find a word. */
        word_length = strcspn(text, whitespace);
        if (word_length == 0)
            break; /* No words left. */

        /* Bookkeep to save word location, then move through text. */
        word[word_count++] = this_word = text;
        text += word_length;
        *(text++) = '\0';
        text += strspn(text, whitespace); /* Move to next word. */

        /* 1- and 2-char words don't count, nor do excessively long ones. */
        if ((word_length < MIN_WORD_LENGTH)
            || (word_length > MAX_WORD_LENGTH)) {
            word_count--;
            continue;
        }

        /* Squash to lowercase. */
        for (chase = this_word; *chase; chase++)
            if (isupper((unsigned char) *chase))
                *chase = tolower((unsigned char) *chase);
    }

    /* If there were no words, we're done. */
    if (word_count < 1)
        return;

    /* Sort the words. */
    std::sort(word, word + word_count, ptr_strcmp);

    /* Count unique words. */
    distinct_words = 0; /* The 1st word is "pre-figured". */
    word_vec[0].index = 0;
    word_vec[0].length = strlen(word[0]);
    word_vec[0].count = 1;

    for (word_index = 1; /* We compare (N-1)th and Nth words. */
         word_index < word_count; word_index++) {
        if (strcmp(word[word_index - 1], word[word_index]) == 0)
            word_vec[distinct_words].count++;
        else {
            distinct_words++;
            word_vec[distinct_words].index = word_index;
            word_vec[distinct_words].length = strlen(word[word_index]);
            word_vec[distinct_words].count = 1;
        }
    }

    /* Sort the counts. */
    distinct_words++; /* We were off-by-1 until this. */
    std::sort(word_vec, word_vec + distinct_words, wvec_freq_cmp);

    /* Sub-sort same-frequency words on word length. */
    for (last = 0, word_index = 1; /* Again, (N-1)th and Nth entries. */
         word_index < distinct_words; word_index++) {
        if (word_vec[last].count != word_vec[word_index].count) {
            if ((word_index - last) != 1) /* 2+ entries to sub-sort. */
                std::sort(&word_vec[last], &word_vec[word_index],
                          wvec_length_cmp);
            last = word_index;
        }
    }
    /* Do it one last time for the only-one-appearance words. */
    if ((word_index - last) != 1)
        std::sort(&word_vec[last], &word_vec[word_index], wvec_length_cmp);

    /* Write the Keywords header field body, at most keylimit bytes. */
    for (word_index = 0; word_index < distinct_words; word_index++) {
        const char *this_one = word[word_vec[word_index].index];
        size_t length = word_vec[word_index].length;

        /* "Noise" words don't count. */
        if (regexec(&preg, this_one, 0, NULL, 0) == 0)
            continue;

        /* Add to list. */
        if (!keywords.empty())
            keywords += ',';
        keywords.append(this_one, std::min<size_t>(length, innconf->keylimit
                                                               - keywords.size()));

        /* Is there enough space to go on?  (comma + longest word) */
        if ((long) keywords.size()
            > (long) (innconf->keylimit - MAX_WORD_LENGTH - 1))
            break;
    }
}

#endif /* DO_KEYWORDS */
