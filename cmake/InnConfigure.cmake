# Platform checks for config.h: the CMake equivalent of configure.ac's tests
# (CMake-only goal item 1).  Each check sets the CMake variable named after
# the config.h symbol, which include/config.h.cmake.in turns into a #define.
# Run-time tests that configure.ac takes from m4/*.m4 use the same programs,
# copied into cmake/checks/.  Platform differences are found by testing,
# never by platform name (CLAUDE.md, "Platforms").

include(CheckCSourceCompiles)
include(CheckCSourceRuns)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckLibraryExists)
include(CheckStructHasMember)
include(CheckSymbolExists)
include(CheckTypeSize)

# Probe with the same feature macros config.h enables, so declarations that
# only appear with them (setgroups, asprintf, ...) are seen.
set(CMAKE_REQUIRED_DEFINITIONS -D_DARWIN_C_SOURCE -D_GNU_SOURCE
    -D__EXTENSIONS__ -D_ALL_SOURCE)

# --- Site settings (configure's --with-* options) ----------------------------

set(INN_ARTFILE_MODE 0664 CACHE STRING "Mode of article files")
set(INN_BATCHFILE_MODE 0664 CACHE STRING "Mode of batch files")
set(INN_GROUPDIR_MODE 0775 CACHE STRING "Mode of newsgroup directories")
set(INN_NEWSUMASK 02 CACHE STRING "umask for INN's programs")
set(INN_NEWSMASTER usenet CACHE STRING
    "Address that mail to the news admin goes to")
set(INN_SYSLOG_FACILITY LOG_NEWS CACHE STRING "syslog facility")
set(INND_PORT "" CACHE STRING "Extra low-numbered port innbind allows")
# RUNASUSER/RUNASGROUP: the news account (configure's --with-news-user and
# --with-news-group).  INN_RUNAS_USER/INN_RUNAS_GROUP (root CMakeLists.txt)
# override them for test builds.
if(INN_RUNAS_USER)
    set(INN_NEWS_USER ${INN_RUNAS_USER})
else()
    set(INN_NEWS_USER news)
endif()
if(INN_RUNAS_GROUP)
    set(INN_NEWS_GROUP ${INN_RUNAS_GROUP})
else()
    set(INN_NEWS_GROUP news)
endif()

# --- Optional features (CMake-only goal item 8 adds options for the rest) ----

set(DO_KEYWORDS 0)
foreach(feature DO_LARGEFILES DO_PERL DO_PYTHON DO_TAGGED_HASH HAVE_BDB
        HAVE_BDB_NDBM HAVE_BLACKLIST HAVE_BLOCKLIST HAVE_CANLOCK HAVE_OPENSSL
        HAVE_SD_NOTIFY)
    set(${feature} FALSE)
endforeach()
set(HAVE_SQLITE3 TRUE)   # find_package(SQLite3 REQUIRED)
set(HAVE_ZLIB TRUE)      # find_package(ZLIB REQUIRED)

# PAM, as configure detects it: the header and the library.
check_include_file(security/pam_appl.h HAVE_SECURITY_PAM_APPL_H)
find_library(INN_PAM_LIB pam)
if(HAVE_SECURITY_PAM_APPL_H AND INN_PAM_LIB)
    set(HAVE_PAM TRUE)
endif()
# PAM_CONST: const if pam_get_item takes a const void ** (m4/pam-const.m4
# greps the preprocessed header for it).  In C a mismatched pointer type is
# only a warning, so the test needs -Werror; it links, so it needs libpam.
set(PAM_CONST "")
if(HAVE_PAM)
    set(CMAKE_REQUIRED_FLAGS -Werror)
    set(CMAKE_REQUIRED_LIBRARIES ${INN_PAM_LIB})
    check_c_source_compiles("
        #include <security/pam_appl.h>
        int get_user(pam_handle_t *pamh) {
            const void *item;
            return pam_get_item(pamh, PAM_USER, &item);
        }
        int main(void) { return 0; }" INN_PAM_GET_ITEM_CONST)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(INN_PAM_GET_ITEM_CONST)
        set(PAM_CONST const)
    endif()
endif()

# Cyrus SASL.
check_include_file(sasl/sasl.h INN_HAVE_SASL_SASL_H)
find_library(INN_SASL_LIB sasl2)
if(INN_HAVE_SASL_SASL_H AND INN_SASL_LIB)
    set(HAVE_SASL TRUE)
endif()

# Kerberos v5, through krb5-config as configure uses it.
find_program(INN_KRB5_CONFIG krb5-config)
if(INN_KRB5_CONFIG)
    execute_process(COMMAND ${INN_KRB5_CONFIG} --cflags krb5
                    OUTPUT_VARIABLE INN_KRB5_CFLAGS
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${INN_KRB5_CONFIG} --libs krb5
                    OUTPUT_VARIABLE INN_KRB5_LIBS
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    separate_arguments(INN_KRB5_CFLAGS)
    separate_arguments(INN_KRB5_LIBS)
    set(CMAKE_REQUIRED_FLAGS_SAVE ${CMAKE_REQUIRED_FLAGS})
    string(JOIN " " CMAKE_REQUIRED_FLAGS ${INN_KRB5_CFLAGS})
    set(CMAKE_REQUIRED_LIBRARIES ${INN_KRB5_LIBS})
    check_include_file(krb5.h HAVE_KRB5_H)
    check_include_file(kerberosv5/krb5.h HAVE_KERBEROSV5_KRB5_H)
    check_include_file(et/com_err.h HAVE_ET_COM_ERR_H)
    check_include_file(kerberosv5/com_err.h HAVE_KERBEROSV5_COM_ERR_H)
    check_include_file(ibm_svc/krb5_svc.h HAVE_IBM_SVC_KRB5_SVC_H)
    check_function_exists(krb5_get_error_message HAVE_KRB5_GET_ERROR_MESSAGE)
    check_function_exists(krb5_free_error_message HAVE_KRB5_FREE_ERROR_MESSAGE)
    if(NOT HAVE_KRB5_GET_ERROR_MESSAGE)
        check_function_exists(krb5_get_error_string HAVE_KRB5_GET_ERROR_STRING)
        check_function_exists(krb5_svc_get_msg HAVE_KRB5_SVC_GET_MSG)
    endif()
    unset(CMAKE_REQUIRED_LIBRARIES)
    set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
    if(HAVE_KRB5_H OR HAVE_KERBEROSV5_KRB5_H)
        set(HAVE_KRB5 TRUE)
    endif()
endif()

# --- Headers -----------------------------------------------------------------

foreach(header crypt.h inttypes.h stdbool.h stdint.h strings.h sys/bitypes.h
        sys/filio.h sys/loadavg.h sys/select.h sys/time.h sys/uio.h syslog.h
        unistd.h)
    string(TOUPPER "HAVE_${header}" var)
    string(REGEX REPLACE "[/.]" "_" var "${var}")
    check_include_file(${header} ${var})
endforeach()

# ndbm: the first of ndbm.h and db1/ndbm.h, then gdbm's two spellings.
check_include_file(ndbm.h HAVE_NDBM_H)
if(NOT HAVE_NDBM_H)
    check_include_file(db1/ndbm.h HAVE_DB1_NDBM_H)
endif()
check_include_file(gdbm/ndbm.h HAVE_GDBM_SLASH_NDBM_H)
if(NOT HAVE_GDBM_SLASH_NDBM_H)
    check_include_file(gdbm-ndbm.h HAVE_GDBM_HYPHEN_NDBM_H)
endif()
# dbm_open in libc, or in one of the dbm libraries (ckpasswd links it).
check_function_exists(dbm_open INN_DBM_OPEN_IN_LIBC)
if(INN_DBM_OPEN_IN_LIBC)
    set(HAVE_DBM TRUE)
    set(INN_DBM_LIBS "")
else()
    foreach(lib ndbm dbm gdbm_compat)
        check_library_exists(${lib} dbm_open "" INN_DBM_OPEN_IN_${lib})
        if(INN_DBM_OPEN_IN_${lib})
            set(HAVE_DBM TRUE)
            set(INN_DBM_LIBS ${lib})
            if(lib STREQUAL gdbm_compat)
                list(APPEND INN_DBM_LIBS gdbm)
            endif()
            break()
        endif()
    endforeach()
endif()

# --- Functions ---------------------------------------------------------------

foreach(func asprintf daemon explicit_bzero fseeko getaddrinfo getgroups
        getloadavg getnameinfo getpagesize getrusage getspnam inet_aton
        inet_ntoa inet_ntop madvise mkstemp pread pwrite reallocarray setbuffer
        setenv seteuid setgroups setrlimit setsid sigaction snprintf
        socketpair strcasecmp strlcat strlcpy strncasecmp strspn strtok
        symlink sysconf)
    string(TOUPPER "HAVE_${func}" var)
    check_function_exists(${func} ${var})
endforeach()
# The first available way to get the file descriptor limit.
check_function_exists(getrlimit HAVE_GETRLIMIT)
if(NOT HAVE_GETRLIMIT)
    check_function_exists(getdtablesize HAVE_GETDTABLESIZE)
    if(NOT HAVE_GETDTABLESIZE)
        check_function_exists(ulimit HAVE_ULIMIT)
    endif()
endif()
# statvfs, or else statfs and the headers that declare it.
check_function_exists(statvfs HAVE_STATVFS)
if(NOT HAVE_STATVFS)
    check_function_exists(statfs HAVE_STATFS)
    check_include_file(sys/vfs.h HAVE_SYS_VFS_H)
    check_include_file(sys/param.h HAVE_SYS_PARAM_H)
    check_include_file(sys/mount.h HAVE_SYS_MOUNT_H)
endif()
# setproctitle in libc or libutil; pstat if neither.
check_function_exists(setproctitle HAVE_SETPROCTITLE)
if(NOT HAVE_SETPROCTITLE)
    check_library_exists(util setproctitle "" INN_SETPROCTITLE_IN_UTIL)
    if(INN_SETPROCTITLE_IN_UTIL)
        set(HAVE_SETPROCTITLE TRUE)
    else()
        check_function_exists(pstat HAVE_PSTAT)
    endif()
endif()
# snprintf is checked above with the other functions.  configure's run test
# (m4/snprintf.m4) also checks for C99 return values, which every platform
# INN++ targets has.
# getgroups' array type (AC_TYPE_GETGROUPS); gid_t on every POSIX system.
set(GETGROUPS_T gid_t)

# --- Declarations ------------------------------------------------------------

foreach(sym pread pwrite reallocarray snprintf setproctitle strlcat strlcpy
        vsnprintf)
    string(TOUPPER "HAVE_DECL_${sym}" var)
    check_symbol_exists(${sym} "stdio.h;stdlib.h;string.h;unistd.h" ${var})
endforeach()
check_symbol_exists(h_errno netdb.h HAVE_DECL_H_ERRNO)
check_symbol_exists(inet_aton "sys/types.h;netinet/in.h;arpa/inet.h"
                    HAVE_DECL_INET_ATON)
check_symbol_exists(inet_ntoa "sys/types.h;netinet/in.h;arpa/inet.h"
                    HAVE_DECL_INET_NTOA)
check_symbol_exists(altzone time.h HAVE_DECL_ALTZONE)

# --- Types and structure members ---------------------------------------------

set(CMAKE_EXTRA_INCLUDE_FILES sys/types.h signal.h sys/socket.h)
check_type_size(sig_atomic_t SIG_ATOMIC_T)
check_type_size(socklen_t SOCKLEN_T)
check_type_size("long long int" LONG_LONG_INT)
check_type_size(_Bool _BOOL)
set(CMAKE_EXTRA_INCLUDE_FILES sys/types.h sys/ipc.h sys/sem.h)
check_type_size("union semun" UNION_SEMUN)
set(CMAKE_EXTRA_INCLUDE_FILES sys/types.h netinet/in.h)
check_type_size("struct sockaddr_in6" STRUCT_SOCKADDR_IN6)
set(CMAKE_EXTRA_INCLUDE_FILES sys/types.h sys/socket.h)
check_type_size("struct sockaddr_storage" STRUCT_SOCKADDR_STORAGE)
unset(CMAKE_EXTRA_INCLUDE_FILES)
# IPv6 is enabled whenever the system has struct sockaddr_in6.
set(HAVE_INET6 ${HAVE_STRUCT_SOCKADDR_IN6})
if(HAVE_STRUCT_SOCKADDR_STORAGE)
    check_struct_has_member("struct sockaddr_storage" ss_family
        "sys/types.h;sys/socket.h" HAVE_STRUCT_SOCKADDR_STORAGE_SS_FAMILY)
endif()
check_struct_has_member("struct sockaddr" sa_len "sys/types.h;sys/socket.h"
                        HAVE_STRUCT_SOCKADDR_SA_LEN)
check_struct_has_member("struct tm" tm_gmtoff time.h HAVE_STRUCT_TM_TM_GMTOFF)
# AC_STRUCT_TIMEZONE: tm_zone, or else the tzname array.
check_struct_has_member("struct tm" tm_zone time.h HAVE_STRUCT_TM_TM_ZONE)
if(NOT HAVE_STRUCT_TM_TM_ZONE)
    check_symbol_exists(tzname time.h HAVE_TZNAME)
endif()
# Large-file support is off (DO_LARGEFILES), so configure's fpos_t test
# (m4/large-fpos.m4) and the fseeko/ftello declaration checks do not run.

# --- Compiler and system behavior --------------------------------------------

check_c_source_compiles("
    #include <stdio.h>
    #define error(...) fprintf(stderr, __VA_ARGS__)
    int main(void) { error(\"foo\"); error(\"foo %d\", 0); return 0; }"
    HAVE_C99_VAMACROS)
check_c_source_compiles("
    #include <stdio.h>
    #define error(args...) fprintf(stderr, args)
    int main(void) { error(\"foo\"); error(\"foo %d\", 0); return 0; }"
    HAVE_GNU_VAMACROS)
check_c_source_compiles("
    #include <sys/types.h>
    #include <sys/mman.h>
    int main(void) { char *p = 0; int psize = 0;
                     return msync(p, psize, MS_ASYNC); }"
    HAVE_MSYNC_3_ARG)
check_c_source_compiles("
    #include <sys/socket.h>
    #include <sys/types.h>
    #ifndef AF_UNIX
    error: No UNIX domain sockets!
    #endif
    int main(void) { return 0; }"
    HAVE_UNIX_DOMAIN_SOCKETS)
check_symbol_exists(SUN_LEN "sys/types.h;sys/un.h" HAVE_SUN_LEN)

# Run-time tests, with the programs from m4/*.m4 (cmake/checks/).  A test
# that cannot run (cross-compiling) gives configure's fallback answer.
# The programs create scratch files in their working directory.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/CMakeFiles/inn-checks)
macro(inn_run_check file result_var)
    try_run(_run _compile ${CMAKE_BINARY_DIR}/CMakeFiles/inn-checks
            ${CMAKE_CURRENT_LIST_DIR}/checks/${file}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/CMakeFiles/inn-checks)
    if(_compile AND _run EQUAL 0)
        set(${result_var} TRUE)
    else()
        set(${result_var} FALSE)
    endif()
endmacro()
inn_run_check(mmap-works.c HAVE_MMAP)
if(HAVE_MMAP)
    inn_run_check(mmap-writes.c INN_MMAP_SEES_WRITES)
    if(NOT INN_MMAP_SEES_WRITES)
        set(MMAP_MISSES_WRITES TRUE)
    endif()
    inn_run_check(mmap-sync.c INN_MMAP_NO_MSYNC)
    if(NOT INN_MMAP_NO_MSYNC)
        set(MMAP_NEEDS_MSYNC TRUE)
    endif()
endif()
inn_run_check(addrconfig.c HAVE_GETADDRINFO_ADDRCONFIG)
inn_run_check(in6-equal.c INN_IN6_ARE_ADDR_EQUAL_WORKS)
if(NOT INN_IN6_ARE_ADDR_EQUAL_WORKS)
    set(HAVE_BROKEN_IN6_ARE_ADDR_EQUAL TRUE)
endif()
# IOV_MAX: configure measures it only when <limits.h> lacks it; every
# platform INN++ targets declares it.
check_symbol_exists(IOV_MAX limits.h INN_HAVE_IOV_MAX)
if(NOT INN_HAVE_IOV_MAX)
    message(FATAL_ERROR "<limits.h> does not declare IOV_MAX")
endif()
# STREAMS descriptor passing (m4/sendfd.m4) exists only where <stropts.h>
# does (Solaris); INN uses Unix-domain sockets otherwise.
check_include_file(stropts.h INN_HAVE_STROPTS_H)
if(INN_HAVE_STROPTS_H)
    message(WARNING "STREAMS fd passing is not tested by the CMake build")
endif()

unset(CMAKE_REQUIRED_DEFINITIONS)
