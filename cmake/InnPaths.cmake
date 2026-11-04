# INN's installation directories and the programs generated files name,
# for the samples and scripts made from .in files (CMake-only goal items 3
# and 4).  These are the values support/fixconfig and support/fixscript
# substitute, with configure's defaults (m4/paths.m4): everything under
# INN_PREFIX (include/CMakeLists.txt).  The names match the @VAR@ names in
# the .in files, so configure_file(... @ONLY) can fill them in.

set(prefix ${INN_PREFIX})
set(exec_prefix ${prefix})
set(bindir ${prefix}/bin)
set(libdir ${prefix}/lib)
set(libexecdir ${prefix}/libexec)
set(sbindir ${prefix}/sbin)
set(sysconfdir ${INN_PATH_CONFIG})
set(tmpdir ${INN_PATH_TMP})
set(CONTROLDIR ${bindir}/control)
set(DBDIR ${prefix}/db)
set(FILTERDIR ${bindir}/filter)
set(HISTORYDIR ${prefix}/db)
set(HTTPDIR ${prefix}/http)
set(LIBPERLDIR ${libdir}/perl)
set(LOGDIR ${prefix}/log)
set(RUNDIR ${prefix}/run)
set(SPOOLDIR ${prefix}/spool)

# Programs.  configure requires Perl (INN_PROG_PERL in configure.ac); the
# root CMakeLists.txt finds it.  Scripts get its full path on their first
# line.
set(PERL ${PERL_EXECUTABLE})
set(SHELL /bin/sh)

# The rest of what configure substitutes into innshellvars,
# innshellvars.pl, innshellvars.tcl and INN::Config (configure.ac,
# m4/compress.m4, m4/modes.m4).  A program configure cannot find becomes its
# bare name, as there.
set(PACKAGE_VERSION ${PROJECT_VERSION})
macro(_inn_prog var)
    find_program(INN_${var} NAMES ${ARGN})
    if(INN_${var})
        set(${var} ${INN_${var}})
    else()
        list(GET ARGN 0 ${var})
    endif()
endmacro()
# AC_PROG_AWK takes the first of these; AC_PROG_EGREP uses grep -E (egrep
# is obsolete).
# (INN_AWK is include/'s plain awk for support/mksystem.)
find_program(INN_SCRIPT_AWK NAMES gawk mawk nawk awk REQUIRED)
set(AWK ${INN_SCRIPT_AWK})
find_program(INN_GREP NAMES ggrep grep REQUIRED)
set(EGREP "${INN_GREP} -E")
_inn_prog(SED sed)
_inn_prog(BZIP2 bzip2)
_inn_prog(CAT cat)
_inn_prog(COMPRESS compress)
_inn_prog(GZIP gzip)
set(SORT ${INN_SORT})
# Programs configure leaves empty when they are missing.
find_program(INN_GPGV NAMES gpgv1 gpgv gpgv2)
find_program(INN_GPG NAMES gpg1 gpg gpg2)
find_program(INN_PGP NAMES pgpv pgp pgpgpg)
foreach(var GPGV GPG PGP)
    if(INN_${var})
        set(${var} ${INN_${var}})
    else()
        set(${var} "")
    endif()
endforeach()
set(GPG_HAS_ALLOW_WEAK_DIGEST_ALGOS_FLAG 0)
if(GPG)
    execute_process(COMMAND ${GPG} --allow-weak-digest-algos --version
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(rc EQUAL 0)
        set(GPG_HAS_ALLOW_WEAK_DIGEST_ALGOS_FLAG 1)
    endif()
endif()
# wget, else ncftp, else INN's own simpleftp.
find_program(INN_PATH_GETFTP NAMES wget ncftpget ncftp)
find_program(INN_PATH_GETHTTP NAMES wget)
foreach(var PATH_GETFTP PATH_GETHTTP)
    if(INN_${var})
        set(${var} ${INN_${var}})
    else()
        set(${var} ${bindir}/simpleftp)
    endif()
endforeach()

# Log compression (configure's --with-log-compress).
set(INN_LOG_COMPRESS gzip CACHE STRING
    "Log compression: gzip, bzip2, compress, cat or a program name")
if(INN_LOG_COMPRESS STREQUAL "gzip")
    set(LOG_COMPRESSEXT .gz)
elseif(INN_LOG_COMPRESS STREQUAL "bzip2")
    set(LOG_COMPRESSEXT .bz2)
elseif(INN_LOG_COMPRESS STREQUAL "compress")
    set(LOG_COMPRESSEXT .Z)
elseif(INN_LOG_COMPRESS STREQUAL "cat")
    set(LOG_COMPRESSEXT "")
else()
    message(FATAL_ERROR "unknown log compression type ${INN_LOG_COMPRESS}")
endif()
string(TOUPPER ${INN_LOG_COMPRESS} lc)
if(NOT INN_${lc})
    message(FATAL_ERROR "${INN_LOG_COMPRESS} not found but specified for "
                        "log compression")
endif()
set(LOG_COMPRESS ${INN_${lc}})
if(INN_GZIP OR NOT INN_COMPRESS)
    set(UNCOMPRESS "${GZIP} -d")
else()
    set(UNCOMPRESS "${COMPRESS} -d")
endif()

# Site settings from cmake/InnConfigure.cmake (m4/modes.m4, m4/users.m4,
# m4/syslog.m4).
set(NEWSUMASK ${INN_NEWSUMASK})
set(FILEMODE ${INN_ARTFILE_MODE})
set(NEWSMASTER ${INN_NEWSMASTER})
set(SYSLOG_FACILITY ${INN_SYSLOG_FACILITY})
# inews used to be installed setgid news, which may not be secure, so only
# on request (configure's --enable-setgid-inews).
option(INN_SETGID_INEWS "Install inews setgid news (mode 02555)" OFF)
if(INN_SETGID_INEWS)
    set(INEWSMODE 02555)
else()
    set(INEWSMODE 0550)
endif()
set(RNEWSMODE 0500)
if(DO_TAGGED_HASH)
    set(DO_DBZ_TAGGED_HASH DO)
else()
    set(DO_DBZ_TAGGED_HASH DONT)
endif()

# inn_fixscript(<input> <output> [NOLIB])
# What support/fixscript does: replace the first line with the path to the
# interpreter (sh or Perl) and, unless NOLIB, the second line with the
# command that loads INN's settings (innshellvars or INN::Config).  The
# output is mode 755.  Runs at configure time, like configure_file.
function(inn_fixscript input output)
    cmake_parse_arguments(F "NOLIB" "" "" ${ARGN})
    file(STRINGS ${input} first LIMIT_COUNT 1)
    if(first MATCHES "/sh$|SH")
        set(path ${SHELL})
        set(lib ". ${libdir}/innshellvars")
    elseif(first MATCHES "/perl|PERL")
        # Keep any interpreter options after the path (#! /usr/bin/perl -w).
        string(REGEX REPLACE "^#! *[^ ]+" "${PERL}" path "${first}")
        set(lib "use lib '${LIBPERLDIR}'; use INN::Config;")
    else()
        message(FATAL_ERROR "${input}: unknown interpreter ${first}")
    endif()
    file(READ ${input} body)
    # Drop the lines fixscript replaces (one, or two with the library line).
    if(F_NOLIB)
        string(REGEX REPLACE "^[^\n]*\n" "" body "${body}")
        set(head "#! ${path}\n")
    else()
        string(REGEX REPLACE "^[^\n]*\n[^\n]*\n" "" body "${body}")
        set(head "#! ${path}\n${lib}\n")
    endif()
    # The rest is copied as is: fixscript substitutes nothing in it.
    file(WRITE ${output}.tmp "${head}${body}")
    file(COPY_FILE ${output}.tmp ${output} ONLY_IF_DIFFERENT)
    file(REMOVE ${output}.tmp)
    file(CHMOD ${output} FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
         GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${input})
endfunction()

# inn_scripts(<name>... [NOLIB])
# inn_fixscript each <name>.in in the current source directory into <name>
# in the current build directory (fixscript -i with NOLIB).
function(inn_scripts)
    cmake_parse_arguments(S "NOLIB" "" "" ${ARGN})
    foreach(name ${S_UNPARSED_ARGUMENTS})
        set(nolib)
        if(S_NOLIB)
            set(nolib NOLIB)
        endif()
        get_filename_component(dir ${CMAKE_CURRENT_BINARY_DIR}/${name}
                               DIRECTORY)
        file(MAKE_DIRECTORY ${dir})
        inn_fixscript(${CMAKE_CURRENT_SOURCE_DIR}/${name}.in
                      ${CMAKE_CURRENT_BINARY_DIR}/${name} ${nolib})
    endforeach()
endfunction()
