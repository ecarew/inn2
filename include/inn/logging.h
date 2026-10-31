/*
**  Where INN's log messages go: syslog (the default) or a file.
**
**  inn_syslog() takes the same arguments as syslog(3), including "%m" for
**  the text of errno, and sends the message to the destination chosen with
**  inn_log_file(): syslog unless a file has been set.  inn_openlog() and
**  inn_closelog() take openlog(3)'s and closelog(3)'s place; the
**  identifier is also used for the lines written to a file.
**
**  A file is opened for appending, so processes sharing it (innd and the
**  children it forks, nnrpd's children) each add whole lines: every message
**  is written with a single write(2).  inn_log_reopen() reopens it by name,
**  for log rotation (news.daily moves the file, then ctlinnd flushlogs).
**
**  Secure logging to another machine is the system syslog daemon's job
**  (syslog-ng or rsyslog forwarding over TLS); INN itself logs locally.
*/

#ifndef INN_LOGGING_H
#define INN_LOGGING_H

#include "inn/portable-macros.h"
#include "inn/portable-stdbool.h"

#include <stdarg.h>

BEGIN_DECLS

/* Like openlog(3) and closelog(3); closelog also closes a log file. */
void inn_openlog(const char *ident, int option, int facility);
void inn_closelog(void);

/* Like syslog(3) and vsyslog(3), to the current destination. */
void inn_syslog(int priority, const char *format, ...)
    __attribute__((__format__(printf, 2, 3)));
void inn_vsyslog(int priority, const char *format, va_list args)
    __attribute__((__format__(printf, 2, 0)));

/* Log to the file at path (created with mode 0640 if missing), or to syslog
   again if path is NULL.  Refuses a symbolic link.  Returns false, logging
   to syslog why and keeping the previous destination, if the file cannot be
   opened. */
bool inn_log_file(const char *path);

/* Reopen the log file by name, after it was moved for rotation.  Nothing to
   do when logging to syslog.  Returns false if it cannot be reopened, in
   which case messages go to syslog until it can. */
bool inn_log_reopen(void);

END_DECLS

#endif /* INN_LOGGING_H */
