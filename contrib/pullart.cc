/*
June 14, 1999

Various bug fixes, code and documentation improvements since then
in 2002, 2003, 2014, 2016, 2021-2024, 2026.

Recover text articles from cyclic buffers
Articles start with  "\0Path:"
and end with "\r\n.\r\n"

Tested with INND 2.2 under AIX 4.2

rifkin@uconn.edu
*/
/*
(1) Pull 16 bytes at a time
(2) Last 7 bytes must be \000\000\000Path
(3) When found, print "\nPath";
(4) print subsequent bytes until \r\n.\r\n found
*/

#include "portable/system.h"

#include <string>

#define INFILE     1
#define FILEPREFIX 2
#define HEADER     3
#define STRING     4

#define MAX_ART_SIZE 2200000

/* Longest file prefix: the name is the prefix plus ".nnnnnn" in 512 bytes. */
#define MAX_PREFIX 384

static const char ArtHead[7] = {0, 0, 0, 'P', 'a', 't', 'h'};
static const char ArtTail[5] = {'\r', '\n', '.', '\r', '\n'};
static const int LenTail = 5;

/*
Writes the article if it matches the requested header field and string,
to a file named fileprefix.fileno.  Returns the number of files written.
*/
static int
WriteArticle(const std::string &art, const char *fileprefix,
             const char *headerin, const char *string, int fileno)
{
    if (headerin != NULL) {
        /*  Find \nHEADER  */
        std::string header = std::string("\n") + headerin;
        size_t beg = art.find(header);

        if (beg == std::string::npos)
            return 0;

        if (string != NULL) {
            /*  Search the header field body, up to the end of its line  */
            size_t end = art.find('\n', beg + 1);

            if (end == std::string::npos)
                return 0;
            if (art.substr(beg + 1, end - beg - 1).find(string)
                == std::string::npos)
                return 0;
        }
    }

    char filename[MAX_PREFIX + 16];
    snprintf(filename, sizeof(filename), "%s.%06i", fileprefix, fileno);

    FILE *outfile = fopen(filename, "wt");
    if (outfile == NULL) {
        printf("Cannot open file name %s\n", filename);
        exit(1);
    }
    fwrite(art.data(), 1, art.size(), outfile);
    fclose(outfile);
    return 1;
}

int
main(int argc, char *argv[])
{
    char buffer[16];
    const char *header = NULL;
    const char *string = NULL;
    int fileno = 0;
    int artno = 0;

    /*  Check number of args  */
    if (argc < 3) {
        printf("Usage:\n");
        printf("  pullart <cycbuff> <fileprefix> [<header> <string>]\n\n");
        printf("  Read cycbuffer <cycbuff> and print all articles whose\n");
        printf("  article header field body <header> contains <string>.\n\n");
        printf("  Articles are written to files name <fileprefix>.nnnnnn\n");
        printf("  where nnnnnn is numbered sequentially from 0.\n\n");
        printf("  If <header> and <string> not specified, all articles\n");
        printf("  are written.\n\n");
        printf("Examples:\n");
        printf("  pullart /news3/cycbuff.3 alt.rec  Newsgroup: alt.rec\n");
        printf("  pullart /news3/cycbuff.3  all\n");
        printf("  pullart firstbuff  article Subject bluejay\n");
        return 1;
    }

    /*  Prevent buffer overflow due to fileprefix too long  */
    if (strlen(argv[FILEPREFIX]) > MAX_PREFIX) {
        printf("cannot have file prefix greater than %d characters\n",
               MAX_PREFIX);
        return 1;
    }

    FILE *Infile = fopen(argv[INFILE], "rb");
    if (Infile == NULL) {
        printf("Cannot open input file.\n");
        return 1;
    }

    if (argc >= 4 && *argv[HEADER] != '\0')
        header = argv[HEADER];
    if (argc >= 5 && *argv[STRING] != '\0')
        string = argv[STRING];

    printf("filename <%s>\n", argv[INFILE]);
    printf("fileprefix <%s>\n", argv[FILEPREFIX]);
    printf("header <%s>\n", header != NULL ? header : "(null)");
    printf("string <%s>\n", string != NULL ? string : "(null)");

    /*  Skip first 0x38000 16byte buffers  */
    if (fseek(Infile, 0x38000L, SEEK_SET) != 0) {
        printf("Cannot seek in input file.\n");
        fclose(Infile);
        return 1;
    }

    /*  Read following 16 byte buffers  */
    std::string art;
    bool ReadingArticle = false;
    int NumTailCharFound = 0;
    while (fread(buffer, 16, 1, Infile) != 0) {

        /*  Found start of article, start collecting it  */
        if (memcmp(buffer + 9, ArtHead, 7) == 0) {
            ReadingArticle = true;
            art.assign("Path");
            continue;
        }

        if (!ReadingArticle)
            continue;

        for (int i = 0; i < 16; i++) {

            /*  Article too big, drop it and move on  */
            if (art.size() >= MAX_ART_SIZE) {
                printf("article number %i bigger than buffer size %i.\n",
                       artno + 1, MAX_ART_SIZE);
                artno++;
                ReadingArticle = false;
                break;
            }

            /*  Add current character to the article, but remove \r  */
            if (buffer[i] != '\r')
                art.push_back(buffer[i]);

            /*  Check for article ending sequence  */
            if (buffer[i] == ArtTail[NumTailCharFound])
                NumTailCharFound++;
            else
                NumTailCharFound = 0;

            /*  End found, write article, reset for next  */
            if (NumTailCharFound == LenTail) {
                ReadingArticle = false;
                NumTailCharFound = 0;
                fileno += WriteArticle(art, argv[FILEPREFIX], header, string,
                                       fileno);
                artno++;
                break;
            }
        }
    }

    fclose(Infile);
    return 0;
}
