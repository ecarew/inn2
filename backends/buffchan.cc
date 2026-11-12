/*
**  Buffered file exploder for innd.
**
**  One Boost.Asio io_context runs everything: the read of the funnel on
**  standard input (inn::LineReader), a steady_timer per site for the -L
**  and -C time limits, and the signals that end the program
**  (inn::ExitSignals).  See support/doxygen-buffchan.md.
*/

#include "portable/system.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unordered_map>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "inn/asio.h"
#include "inn/innconf.h"
#include "inn/libinn.h"
#include "inn/messages.h"
#include "inn/newsuser.h"
#include "inn/paths.h"
#include "inn/qio.h"

namespace asio = boost::asio;

namespace {

/* Closes a site's file; SITEclose reports close errors itself first. */
struct FileCloser {
    void operator()(FILE *f) const { fclose(f); }
};
using File = std::unique_ptr<FILE, FileCloser>;

/*
**  One active site.  Sites live in an unordered_map and are never erased,
**  so a timer's handler may hold a Site *.
*/
struct Site {
    explicit Site(asio::io_context &io) : flushTimer(io), closeTimer(io) {}

    std::string name;
    std::string filename;
    File f;
    std::unique_ptr<char[]> buffer; /* -b: the stdio buffer */
    bool dropped = false;
    int closeLines = 0;
    int flushLines = 0;
    asio::steady_timer flushTimer; /* -L */
    asio::steady_timer closeTimer; /* -C */
};

/* Options. */
std::string Format;
std::string Map;
int BufferMode;
int CloseEvery;
int FlushEvery;
int CloseSeconds;
int FlushSeconds;
int Fields = 1;

/* The site-name map (-m): site name to the name used in the file name.  The
   first line for a name wins. */
std::unordered_map<std::string, std::string> MapData;

std::unordered_map<std::string, Site> Sites;
asio::io_context *IO;


/*
**  Read the map file.
*/
void
MAPread(const std::string &name)
{
    FILE *F;
    char buff[BUFSIZ];

    MapData.clear();
    if ((F = fopen(name.c_str(), "r")) == nullptr) {
        fprintf(stderr, "Can't open %s, %s\n", name.c_str(), strerror(errno));
        exit(1);
    }

    /* Read each line; ignore blank and comment lines. */
    while (fgets(buff, sizeof buff, F) != nullptr) {
        std::string line(buff);
        if (!line.empty() && line.back() == '\n')
            line.pop_back();
        const std::string::size_type colon = line.find(':');
        if (line.empty() || line[0] == '#' || colon == std::string::npos)
            continue;
        MapData.emplace(line.substr(0, colon), line.substr(colon + 1));
    }
    fclose(F);
}


/*
**  Close a site's file and stop its time limits.
*/
void
SITEclose(Site &s)
{
    s.flushTimer.cancel();
    s.closeTimer.cancel();
    if (s.f) {
        FILE *F = s.f.get();
        bool bad = fflush(F) == EOF || ferror(F) || fchmod(fileno(F), 0664) < 0;
        if (fclose(s.f.release()) == EOF)
            bad = true;
        if (bad)
            syswarn("%s cannot close %s", s.name.c_str(), s.filename.c_str());
    }
}

void
SITEcloseall()
{
    for (auto &entry : Sites)
        SITEclose(entry.second);
}

void SITEflush(Site &s);

/*
**  Write out a site's buffered data.
*/
void
SITEfflush(Site &s)
{
    if (fflush(s.f.get()) == EOF || ferror(s.f.get()))
        syswarn("%s cannot flush %s", s.name.c_str(), s.filename.c_str());
    s.flushLines = 0;
}

/*
**  Arm a site's time limits.  Each acts only while the file is open.
*/
void
armFlushTimer(Site &s)
{
    if (!FlushSeconds)
        return;
    s.flushTimer.expires_after(std::chrono::seconds(FlushSeconds));
    s.flushTimer.async_wait([sp = &s](const boost::system::error_code &ec) {
        if (ec || !sp->f)
            return;
        SITEfflush(*sp);
        armFlushTimer(*sp);
    });
}

void
armCloseTimer(Site &s)
{
    if (!CloseSeconds)
        return;
    s.closeTimer.expires_after(std::chrono::seconds(CloseSeconds));
    s.closeTimer.async_wait([sp = &s](const boost::system::error_code &ec) {
        if (ec || !sp->f)
            return;
        SITEflush(*sp);
    });
}


/*
**  Open the file for a site.
*/
void
SITEopen(Site &s)
{
    const char *path = s.filename.c_str();

    s.f.reset(xfopena(path));
    if (!s.f && errno == EACCES && chmod(path, 0644) == 0)
        s.f.reset(xfopena(path));
    if (!s.f) {
        syswarn("%s cannot fopen %s", s.name.c_str(), path);
        s.f.reset(fopen("/dev/null", "w"));
        if (!s.f)
            /* This really should not happen. */
            sysdie("%s cannot fopen /dev/null", s.name.c_str());
    } else if (fchmod(fileno(s.f.get()), 0444) < 0)
        syswarn("%s cannot fchmod %s", s.name.c_str(), path);

    if (BufferMode == 'b')
        setvbuf(s.f.get(), s.buffer.get(), _IOFBF, BUFSIZ);
    else if (BufferMode == 'u')
        setvbuf(s.f.get(), nullptr, _IONBF, 0);

    /* Reset all counters and limits. */
    s.flushLines = 0;
    s.closeLines = 0;
    s.dropped = false;
    armFlushTimer(s);
    armCloseTimer(s);
}


/*
**  Find a site, possibly creating it.
*/
Site *
SITEfind(const std::string &name, bool canCreate)
{
    auto it = Sites.find(name);
    if (it != Sites.end())
        return &it->second;
    if (!canCreate)
        return nullptr;

    Site &s = Sites.try_emplace(name, *IO).first->second;
    s.name = name;

    std::string fileSite = name;
    if (!Map.empty()) {
        auto m = MapData.find(name);
        if (m != MapData.end())
            fileSite = m->second;
    }
    char buff[BUFSIZ];
#if __GNUC__ > 4 || defined(__llvm__) || defined(__clang__)
#    pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    snprintf(buff, sizeof(buff), Format.c_str(), fileSite.c_str());
#if __GNUC__ > 4 || defined(__llvm__) || defined(__clang__)
#    pragma GCC diagnostic warning "-Wformat-nonliteral"
#endif
    s.filename = buff;
    if (BufferMode == 'b')
        s.buffer = std::make_unique<char[]>(BUFSIZ);
    SITEopen(s);
    return &s;
}


/*
**  Flush a site: close and reopen its file, unless it was dropped.
*/
void
SITEflush(Site &s)
{
    SITEclose(s);
    if (!s.dropped)
        SITEopen(s);
}

void
SITEflushall()
{
    for (auto &entry : Sites)
        SITEflush(entry.second);
}


/*
**  Write data to a site.
*/
void
SITEwrite(const std::string &name, const std::string &data)
{
    Site &s = *SITEfind(name, true);
    if (!s.f)
        SITEopen(s);

    if (fwrite(data.data(), 1, data.size(), s.f.get()) != data.size())
        syswarn("%s cannot write", s.name.c_str());

    /* Bump line counts; see if time to close or flush. */
    if (CloseEvery && ++s.closeLines >= CloseEvery) {
        SITEflush(s);
        return;
    }
    if (FlushEvery && ++s.flushLines >= FlushEvery) {
        SITEfflush(s);
        armFlushTimer(s);
    }
}


/*
**  The rest of a command after its keyword and any white space.
*/
std::string
argument(const std::string &cmd, std::string::size_type keyword)
{
    std::string::size_type i = keyword;
    while (i < cmd.size() && ISWHITE(cmd[i]))
        i++;
    return cmd.substr(i);
}

/*
**  Handle a command message.
*/
void
Process(const std::string &cmd)
{
    if (cmd.starts_with("begin"))
        /* No-op. */
        return;

    if (cmd.starts_with("flush")) {
        const std::string site = argument(cmd, 5);
        Site *s;
        if (site.empty())
            SITEflushall();
        else if ((s = SITEfind(site, false)) != nullptr)
            SITEflush(*s);
        return;
    }

    if (cmd.starts_with("drop")) {
        const std::string site = argument(cmd, 4);
        Site *s;
        if (site.empty())
            SITEcloseall();
        else if ((s = SITEfind(site, false)) == nullptr)
            warn("drop %s unknown site", site.c_str());
        else {
            SITEclose(*s);
            s->dropped = true;
        }
        return;
    }

    if (cmd.starts_with("readmap")) {
        MAPread(Map);
        return;
    }

    /* Other command messages -- ignored. */
    warn("unknown message %s", cmd.c_str());
}


/*
**  Handle one input line, without its newline.
*/
void
handleLine(const std::string &line)
{
    /* Command?  "!!" starts a data line that begins with "!". */
    std::string::size_type start = 0;
    if (!line.empty() && line[0] == EXP_CONTROL) {
        if (line.size() < 2 || line[1] != EXP_CONTROL) {
            Process(line.substr(1));
            return;
        }
        start = 1;
    }

    /* Skip the right number of leading fields. */
    std::string::size_type p = start;
    for (int i = Fields; p < line.size(); p++)
        if (line[p] == ' ' && --i <= 0)
            break;
    if (p >= line.size())
        /* Nothing to write.  Probably shouldn't happen. */
        return;

    /* The leading fields, with a newline, go to each site named after
       them. */
    static std::string data; /* reused, to save an allocation per line */
    data.assign(line, start, p - start);
    data += '\n';
    for (p++; p < line.size();) {
        while (p < line.size() && line[p] == ' ')
            p++;
        std::string::size_type stop = line.find(' ', p);
        if (stop == std::string::npos)
            stop = line.size();
        if (stop > p)
            SITEwrite(line.substr(p, stop - p), data);
        p = stop;
    }
}


/*
**  Close everything and end the event loop.
*/
void
finish()
{
    SITEcloseall();
    IO->stop();
}


} // namespace


int
main(int ac, char *av[])
{
    int i;
    const char *Directory;
    bool Redirect;
    FILE *F;
    char *path;

    /* First thing, set up our identity. */
    message_program_name = "buffchan";
    ensure_not_root();

    /* Set defaults. */
    if (!innconf_read(nullptr))
        exit(1);
    path = concatpath(innconf->pathlog, INN_PATH_ERRLOG);
    const std::string ERRLOG = path;
    free(path);
    Directory = nullptr;
    Redirect = true;
    umask(NEWSUMASK);

    asio::io_context io;
    IO = &io;

    /* The first of these signals closes the files and ends the program; a
       second one kills it at once. */
    inn::ExitSignals signals(
        io, {SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, SIGALRM},
        [](int) { finish(); });

    /* Parse JCL. */
    while ((i = getopt(ac, av, "bc:C:d:f:l:L:m:p:rs:u")) != EOF)
        switch (i) {
        default:
            die("usage error");
            /* NOTREACHED */
        case 'b':
        case 'u':
            BufferMode = i;
            break;
        case 'c':
            CloseEvery = atoi(optarg);
            break;
        case 'C':
            CloseSeconds = atoi(optarg);
            break;
        case 'd':
            Directory = optarg;
            if (Format.empty())
                Format = "%s";
            break;
        case 'f':
            Fields = atoi(optarg);
            break;
        case 'l':
            FlushEvery = atoi(optarg);
            break;
        case 'L':
            FlushSeconds = atoi(optarg);
            break;
        case 'm':
            Map = optarg;
            MAPread(Map);
            break;
        case 'p':
            if ((F = fopen(optarg, "w")) == nullptr)
                sysdie("cannot fopen %s", optarg);
            fprintf(F, "%ld\n", (long) getpid());
            if (ferror(F) || fclose(F) == EOF)
                sysdie("cannot fclose %s", optarg);
            break;
        case 'r':
            Redirect = false;
            break;
        case 's':
            Format = optarg;
            break;
        }
    ac -= optind;
    if (ac)
        die("usage error");

    /* Do some basic set-ups. */
    if (Redirect) {
        if (freopen(ERRLOG.c_str(), "a", stderr) == nullptr)
            sysdie("cannot open %s for error output", ERRLOG.c_str());
    }
    if (Format.empty()) {
        path = concatpath(innconf->pathoutgoing, "%s");
        Format = path;
        free(path);
    }
    if (Directory && chdir(Directory) < 0)
        sysdie("cannot chdir to %s", Directory);

    /* Read input until end of file or a signal.  Lines follow QIO's rules
       (include/inn/asio.h): a line of 32 KiB or more is skipped with a
       warning, and a partial line at end of file is dropped. */
    inn::LineReader reader(io, STDIN_FILENO);
    reader.start(handleLine, [] { warn("long line"); },
                 [](const boost::system::error_code &ec) {
                     if (ec != boost::asio::error::eof) {
                         errno = ec.value();
                         syswarn("cannot read");
                     }
                     finish();
                 });
    io.run();
    exit(0);
    /* NOTREACHED */
}
