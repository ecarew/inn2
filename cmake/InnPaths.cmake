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

# configure requires Perl (INN_PROG_PERL in configure.ac); scripts get its
# full path on their first line.
find_program(INN_PERL perl REQUIRED)
set(PERL ${INN_PERL})
set(SHELL /bin/sh)

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
