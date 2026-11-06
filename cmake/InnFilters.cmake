# Embedded Perl and Python: innd's and nnrpd's filters and nnrpd's
# authentication and access hooks.  Converted from configure's --with-perl
# and --with-python (m4/perl.m4, m4/python.m4).  Off by default, as there.
#
# Sets DO_PERL and DO_PYTHON for config.h, and INN_FILTER_PERL_CPPFLAGS,
# INN_FILTER_PERL_LIBS, INN_FILTER_PYTHON_CPPFLAGS and
# INN_FILTER_PYTHON_LIBS for the root CMakeLists.txt.  Included from
# cmake/InnConfigure.cmake, after it has cleared DO_PERL and DO_PYTHON.

option(INN_PERL_FILTERS
       "Embedded Perl filters and nnrpd hooks (configure's --with-perl)" OFF)
option(INN_PYTHON_FILTERS
       "Embedded Python filters and nnrpd hooks (configure's --with-python)"
       OFF)

set(INN_FILTER_PERL_CPPFLAGS "")
set(INN_FILTER_PERL_LIBS "")
set(INN_FILTER_PYTHON_CPPFLAGS "")
set(INN_FILTER_PYTHON_LIBS "")

if(INN_PERL_FILTERS)
    # As INN_LIB_PERL: the flags ExtUtils::Embed gives, with Perl's include
    # directories as system directories (its headers are not warning-clean)
    # and without -lc, or -lgdbm on Linux.
    execute_process(
        COMMAND ${PERL_EXECUTABLE} -MExtUtils::Embed -e ccopts
        OUTPUT_VARIABLE _ccopts OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "INN_PERL_FILTERS needs ExtUtils::Embed "
                            "(${PERL_EXECUTABLE} -MExtUtils::Embed failed)")
    endif()
    execute_process(
        COMMAND ${PERL_EXECUTABLE} -MExtUtils::Embed -e ldopts
        OUTPUT_VARIABLE _ldopts OUTPUT_STRIP_TRAILING_WHITESPACE)
    separate_arguments(_ccopts UNIX_COMMAND "${_ccopts}")
    separate_arguments(_ldopts UNIX_COMMAND "${_ldopts}")
    foreach(_f IN LISTS _ccopts)
        if(_f MATCHES "^-I(.+)$")
            list(APPEND INN_FILTER_PERL_CPPFLAGS "-isystem${CMAKE_MATCH_1}")
        elseif(_f MATCHES "^-D_LARGEFILE_SOURCE$|^-D_FILE_OFFSET_BITS=|^-D_LARGE_FILES$")
            # Dropped as configure drops them without large-file support,
            # so that these files agree with the rest of INN on off_t.
        elseif(_f MATCHES "^-D|^-f|^-W")
            # Compiler options Perl was built with (-O, -pipe, -arch and
            # the like are the build's own business); configure kept them
            # all, but -D and -f ones are those that change what Perl's
            # headers declare.
            list(APPEND INN_FILTER_PERL_CPPFLAGS "${_f}")
        endif()
    endforeach()
    foreach(_f IN LISTS _ldopts)
        if(_f STREQUAL "-lc")
            continue()
        endif()
        if(_f STREQUAL "-lgdbm" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            continue()
        endif()
        list(APPEND INN_FILTER_PERL_LIBS "${_f}")
    endforeach()
    set(DO_PERL TRUE)
    message(STATUS "Embedded Perl: ${PERL_EXECUTABLE}")
endif()

if(INN_PYTHON_FILTERS)
    # Python 3.3 or later, as configure; Python 2 is no longer supported.
    find_package(Python3 3.3 REQUIRED COMPONENTS Interpreter Development.Embed)
    foreach(_d IN LISTS Python3_INCLUDE_DIRS)
        list(APPEND INN_FILTER_PYTHON_CPPFLAGS "-isystem${_d}")
    endforeach()
    set(INN_FILTER_PYTHON_LIBS Python3::Python)
    set(DO_PYTHON TRUE)
    message(STATUS "Embedded Python: ${Python3_EXECUTABLE} "
                   "(${Python3_VERSION})")
endif()
