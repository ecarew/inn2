# Generated headers (CMake-only goal item 1): what ./configure and
# include/Makefile wrote into the source tree, written here into the build
# tree's include directory at configure time.  The root CMakeLists.txt
# writes config.h itself (cmake/InnConfigure.cmake) before this runs.

set(out ${CMAKE_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${out}/inn)

# inn/system.h: the config.h results INN's public headers need, with INN_
# prefixes (support/mksystem, as include/Makefile runs it).
find_program(INN_AWK NAMES awk REQUIRED)
execute_process(
    COMMAND sh ${CMAKE_SOURCE_DIR}/support/mksystem ${INN_AWK} ${out}/config.h
    OUTPUT_FILE ${out}/inn/system.h.new
    RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "support/mksystem failed")
endif()
file(COPY_FILE ${out}/inn/system.h.new ${out}/inn/system.h ONLY_IF_DIFFERENT)
file(REMOVE ${out}/inn/system.h.new)

# inn/version.h (support/mkversion).  VERSION_EXTRA is Makefile.global's.
set(INN_VERSION_EXTRA prerelease CACHE STRING
    "Version suffix (\"prerelease\" adds the date of the last commit)")
execute_process(
    COMMAND sh ${CMAKE_SOURCE_DIR}/support/mkversion ${PROJECT_VERSION}
            ${INN_VERSION_EXTRA}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_FILE ${out}/inn/version.h.new
    RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "support/mkversion failed")
endif()
file(COPY_FILE ${out}/inn/version.h.new ${out}/inn/version.h ONLY_IF_DIFFERENT)
file(REMOVE ${out}/inn/version.h.new)

# inn/paths.h from paths.h.in: the inn.conf and temporary directories
# (configure's --sysconfdir and --with-tmp-dir, m4/paths.m4) and three
# programs found on PATH.  INN_PREFIX is configure's default prefix.
set(INN_PREFIX /usr/local/news CACHE PATH "INN's installation prefix")
set(INN_PATH_CONFIG ${INN_PREFIX}/etc CACHE PATH "Directory of inn.conf")
set(INN_PATH_TMP ${INN_PREFIX}/tmp CACHE PATH "Directory for temporary files")
find_program(INN_COMPRESS compress)
find_program(INN_GZIP gzip)
find_program(INN_SORT sort REQUIRED)
set(configure_input "inn/paths.h.  Generated from paths.h.in by CMake.")
set(PATH_CONFIG ${INN_PATH_CONFIG})
set(PATH_TMP ${INN_PATH_TMP})
# configure substitutes the bare name when it cannot find a program.
if(INN_COMPRESS)
    set(COMPRESS ${INN_COMPRESS})
else()
    set(COMPRESS compress)
endif()
if(INN_GZIP)
    set(GZIP ${INN_GZIP})
else()
    set(GZIP gzip)
endif()
set(SORT ${INN_SORT})
configure_file(inn/paths.h.in ${out}/inn/paths.h @ONLY)

# inn/portable-*.h: copies of portable/*.h for the installed headers, with
# their includes pointed at inn/ and their config.h symbols renamed to the
# INN_ ones in inn/system.h.  The same plain substring replacements, in the
# same order, as include/Makefile's sed rules.
function(inn_portable_header name)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/portable/${name}.h)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${src})
    file(READ ${src} text)
    string(REPLACE "\"portable/" "\"inn/portable-" text "${text}")
    foreach(symbol ${ARGN})
        string(REPLACE "${symbol}" "INN_${symbol}" text "${text}")
    endforeach()
    if(ARGN)
        string(REPLACE "config.h" "inn/system.h" text "${text}")
        string(REPLACE "portable/socket.h" "inn/portable-socket.h"
               text "${text}")
    endif()
    file(WRITE ${out}/inn/portable-${name}.h.new "${text}")
    file(COPY_FILE ${out}/inn/portable-${name}.h.new
         ${out}/inn/portable-${name}.h ONLY_IF_DIFFERENT)
    file(REMOVE ${out}/inn/portable-${name}.h.new)
endfunction()
inn_portable_header(getaddrinfo HAVE_GETADDRINFO)
inn_portable_header(getnameinfo HAVE_GETNAMEINFO)
inn_portable_header(macros)
inn_portable_header(socket HAVE_BROKEN_IN6_ARE_ADDR_EQUAL
    HAVE_GETADDRINFO_ADDRCONFIG HAVE_LONG_LONG_INT HAVE_SOCKLEN_T
    HAVE_STRUCT_SOCKADDR_SA_LEN HAVE_STRUCT_SOCKADDR_STORAGE
    HAVE_DECL_INET_ATON HAVE_DECL_INET_NTOA HAVE_INET_ATON HAVE_INET_NTOA
    HAVE_INET_NTOP)
inn_portable_header(stdbool HAVE__BOOL HAVE_STDBOOL_H)
