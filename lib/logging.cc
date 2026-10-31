/*
**  Where INN's log messages go: syslog (the default) or a file.  See
**  include/inn/logging.h.
*/

#include "portable/system.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <syslog.h>
#include <unistd.h>

#include "inn/logging.h"

/* The identifier from inn_openlog; openlog(3) keeps a pointer to it. */
static std::string ident = "inn";

/* The log file's name, and its descriptor; empty and -1 for syslog.  The
   name stays set while the file cannot be reopened (descriptor -1), so
   messages go to syslog until inn_log_reopen succeeds. */
static std::string path;
static int fd = -1;

static const char *
level_name(int priority)
{
    switch (LOG_PRI(priority)) {
    case LOG_EMERG:
        return "emerg";
    case LOG_ALERT:
        return "alert";
    case LOG_CRIT:
        return "crit";
    case LOG_ERR:
        return "err";
    case LOG_WARNING:
        return "warning";
    case LOG_NOTICE:
        return "notice";
    case LOG_INFO:
        return "info";
    default:
        return "debug";
    }
}

/* The format with each "%m" replaced by the text of err, as syslog(3) does;
   "%%" is kept for vsnprintf. */
static std::string
expand_errno(const char *format, int err)
{
    std::string out;

    for (const char *p = format; *p != '\0'; p++) {
        if (p[0] == '%' && (p[1] == '%' || p[1] == 'm')) {
            if (p[1] == '%')
                out += "%%";
            else
                for (const char *e = strerror(err); *e != '\0'; e++) {
                    if (*e == '%')
                        out += '%';
                    out += *e;
                }
            p++;
        } else
            out += *p;
    }
    return out;
}

/* The message formatted, with control characters other than tab written as
   "#" and three octal digits (as rsyslog does), so a message cannot start a
   line of its own, and without trailing newlines. */
static std::string __attribute__((__format__(printf, 1, 0)))
format_message(const char *format, va_list args)
{
    va_list copy;
    int length;
    std::string raw, out;

    va_copy(copy, args);
    length = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length < 0)
        return "(cannot format log message)";
    raw.resize(static_cast<size_t>(length) + 1);
    vsnprintf(raw.data(), raw.size(), format, args);
    raw.resize(static_cast<size_t>(length));
    while (!raw.empty() && raw.back() == '\n')
        raw.pop_back();

    for (unsigned char c : raw) {
        if ((c < 0x20 && c != '\t') || c == 0x7f) {
            char octal[5];

            snprintf(octal, sizeof(octal), "#%03o", c);
            out += octal;
        } else
            out += static_cast<char>(c);
    }
    return out;
}

static int
open_file(const char *name)
{
    return open(name, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                0640);
}

/* Write one line to the log file in a single write(2).  Returns false if
   it was not all written. */
static bool
write_line(int priority, const std::string &message)
{
    char stamp[64];
    time_t now = time(nullptr);
    struct tm tm;
    std::string line;
    ssize_t status;

    if (localtime_r(&now, &tm) == nullptr
        || strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S%z", &tm) == 0)
        snprintf(stamp, sizeof(stamp), "%ld", static_cast<long>(now));
    line = std::string(stamp) + " " + ident + "["
           + std::to_string(static_cast<long>(getpid()))
           + "]: " + level_name(priority) + ": " + message + "\n";
    do
        status = write(fd, line.data(), line.size());
    while (status < 0 && errno == EINTR);
    return status == static_cast<ssize_t>(line.size());
}


void
inn_openlog(const char *name, int option, int facility)
{
    if (name != nullptr)
        ident = name;
    openlog(ident.c_str(), option, facility);
}


void
inn_closelog(void)
{
    closelog();
    if (fd >= 0)
        close(fd);
    fd = -1;
    path.clear();
}


void
inn_vsyslog(int priority, const char *format, va_list args)
{
    int err = errno;
    const std::string expanded = expand_errno(format, err);
    const std::string message = format_message(expanded.c_str(), args);

    if (fd < 0 || !write_line(priority, message))
        syslog(priority, "%s", message.c_str());
    errno = err;
}


void
inn_syslog(int priority, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    inn_vsyslog(priority, format, args);
    va_end(args);
}


bool
inn_log_file(const char *name)
{
    int newfd;

    if (name == nullptr) {
        if (fd >= 0)
            close(fd);
        fd = -1;
        path.clear();
        return true;
    }
    newfd = open_file(name);
    if (newfd < 0) {
        syslog(LOG_ERR, "cannot open log file %s: %m", name);
        return false;
    }
    if (fd >= 0)
        close(fd);
    fd = newfd;
    path = name;
    return true;
}


bool
inn_log_reopen(void)
{
    int newfd;

    if (path.empty())
        return true;
    newfd = open_file(path.c_str());
    if (fd >= 0)
        close(fd);
    fd = newfd;
    if (fd < 0) {
        syslog(LOG_ERR, "cannot reopen log file %s: %m", path.c_str());
        return false;
    }
    return true;
}
