# INN's tests for CTest (CMake-only goal item 9): `ctest --test-dir build`.
# The test programs stay C and are built as support/verify-cxx.sh builds
# them, which also checks symbols and builds that CTest does not.
#
# Tests run where support/verify-cxx.sh runs them.  Most run in tests/ in
# the source tree, which holds their helpers and data (CNFS also rejects the
# longer build-tree path).  The program tests (authprogs, lib/innconf and
# the shell tests) reach programs as ../../<dir>/<prog> from tests/data,
# the Make build's in-tree layout, so they run in a sandbox in the build
# tree: a copy of tests/data with the build's program directories beside
# it.  Tests share these directories and some use fixed ports, so they hold
# one resource lock and run one at a time even under ctest -j.

set(sandbox ${CMAKE_CURRENT_BINARY_DIR}/sandbox)

add_library(inn_tap STATIC tap/basic.c tap/float.c tap/messages.c
            tap/process.c tap/string.c)
target_include_directories(inn_tap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_SOURCE_DIR}/history ${CMAKE_SOURCE_DIR}/storage)
# libinnstorage and libinnhist use each other; history/CMakeLists.txt makes
# CMake name libinnstorage again for GNU ld.
target_link_libraries(inn_tap PUBLIC INN::storage INN::hist INN::inn
                      ZLIB::ZLIB SQLite3::SQLite3 m)

# inn_tap_test(name [SOURCE src] [DEFINE def] [EXTRA src...] [OBJECTS tgt...]
#              [OUTPUT path])
# Build tests/<SOURCE or name>-t.c into a test program and register it as
# <name>.  EXTRA sources are compiled in: lib/X is the file under test,
# built with -DTESTING as tests/Makefile does (X.cc once converted, else
# X.c), anything else is test support.  OBJECTS are object libraries.
function(inn_tap_test name)
    cmake_parse_arguments(T "" "SOURCE;DEFINE;OUTPUT" "EXTRA;OBJECTS" ${ARGN})
    if(NOT T_SOURCE)
        set(T_SOURCE ${name})
    endif()
    string(REPLACE "/" "_" target test_${name})
    set(srcs ${T_SOURCE}-t.c)
    foreach(e ${T_EXTRA})
        if(EXISTS ${CMAKE_SOURCE_DIR}/${e}.cc)
            set(f ${CMAKE_SOURCE_DIR}/${e}.cc)
        else()
            set(f ${CMAKE_SOURCE_DIR}/${e}.c)
        endif()
        if(e MATCHES "^lib/")
            set_source_files_properties(${f} PROPERTIES
                                        COMPILE_DEFINITIONS TESTING)
        endif()
        list(APPEND srcs ${f})
    endforeach()
    foreach(o ${T_OBJECTS})
        list(APPEND srcs $<TARGET_OBJECTS:${o}>)
    endforeach()
    add_executable(${target} ${srcs})
    target_link_libraries(${target} PRIVATE inn_tap)
    if(T_DEFINE)
        target_compile_definitions(${target} PRIVATE ${T_DEFINE})
    endif()
    # The C test and the C++ library code link as C++.
    set_target_properties(${target} PROPERTIES LINKER_LANGUAGE CXX)
    if(T_OUTPUT)
        get_filename_component(dir ${T_OUTPUT} DIRECTORY)
        get_filename_component(file ${T_OUTPUT} NAME)
        set_target_properties(${target} PROPERTIES
                              RUNTIME_OUTPUT_DIRECTORY ${dir} OUTPUT_NAME ${file})
        return()
    endif()
    set(dir ${CMAKE_CURRENT_SOURCE_DIR})
    if(name MATCHES "^authprogs/" OR name STREQUAL "lib/innconf")
        set(dir ${sandbox}/tests)
    endif()
    add_test(NAME ${name}
             COMMAND ${CMAKE_SOURCE_DIR}/support/ctest-tap.sh ${name}
                     $<TARGET_FILE:${target}>
             WORKING_DIRECTORY ${dir})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77
                         FIXTURES_REQUIRED inn_sandbox RESOURCE_LOCK inn_sandbox
                         ENVIRONMENT "${inn_test_env}")
endfunction()

# inn_shell_test(name): tests/<name>.t, a shell test.
function(inn_shell_test name)
    add_test(NAME ${name}
             COMMAND ${CMAKE_SOURCE_DIR}/support/ctest-tap.sh ${name}
                     sh ${CMAKE_CURRENT_SOURCE_DIR}/${name}.t
             WORKING_DIRECTORY ${sandbox}/tests)
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77
                         FIXTURES_REQUIRED inn_sandbox RESOURCE_LOCK inn_sandbox
                         ENVIRONMENT "${inn_test_env}")
endfunction()

# lib/hissqlite-convert and overview/ovsqlite-integ.t run these programs by
# these variables; their defaults are the Make build's locations.
set(inn_test_env
    HISSQLITE_CONVERT=$<TARGET_FILE:hissqlite-convert>
    OVSQLITE_SERVER=$<TARGET_FILE:ovsqlite-server>)

# A fresh sandbox before the tests run.  overview/ovsqlite-integ.t also
# reads ../include/config.h from it.
add_test(NAME sandbox
         COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_SOURCE_DIR}
                 -DBUILD=${CMAKE_BINARY_DIR} -DSANDBOX=${sandbox}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/sandbox.cmake)
set_tests_properties(sandbox PROPERTIES FIXTURES_SETUP inn_sandbox)

# overview/ovsqlite-integ.t expects these helpers beside it.
foreach(h write read)
    inn_tap_test(ovsqlite-${h} SOURCE overview/ovsqlite-${h}
                 OUTPUT ${sandbox}/tests/overview/ovsqlite-${h}.t)
endforeach()

foreach(t artnumber bloom bloom-hiswalk buffer canlock concat conffile
          confparse date dispatch fdflag hash hashtab headers hex hissqlite
          hissqlite-convert innconf list md5 messageid messages newsuser
          network/addr-ipv4 network/addr-ipv6 network/client network/server
          qio readin reservedfd tst uwildmat vector wire)
    inn_tap_test(lib/${t})
endforeach()
foreach(t asprintf daemon getaddrinfo getnameinfo inet_aton inet_ntoa
          inet_ntop mkstemp pread pwrite reallocarray setenv snprintf
          strlcat strlcpy)
    inn_tap_test(lib/${t} EXTRA lib/${t})
endforeach()
inn_tap_test(lib/xwrite EXTRA lib/xwrite tests/lib/fakewrite)

inn_tap_test(authprogs/ident)
inn_tap_test(authprogs/identport)
inn_tap_test(authprogs/radius)
inn_shell_test(authprogs/ckpasswd)
inn_shell_test(authprogs/domain)

inn_tap_test(nnrpd/auth-ext EXTRA nnrpd/auth-ext)

# innd tests link innd_core with tests/innd/fakeinnd.c in place of innd.cc,
# as tests/Makefile does.
foreach(t artparse chan)
    inn_tap_test(innd/${t} EXTRA tests/innd/fakeinnd
                 OBJECTS inn_canlock inn_perl)
    target_link_libraries(test_innd_${t} PRIVATE innd_core)
endforeach()

inn_tap_test(expire/tombstone)
inn_tap_test(expire/tombstone-hisexpire)
inn_shell_test(expire/tombstone-e2e)

inn_tap_test(overview/api)
foreach(ov buffindexed tradindexed)
    inn_tap_test(overview/${ov} SOURCE overview/overview DEFINE OVTYPE=${ov})
endforeach()
inn_tap_test(overview/ovsqlite)
inn_shell_test(overview/ovsqlite-integ)
inn_tap_test(overview/tdx-group)
inn_shell_test(overview/tdx-util)
inn_tap_test(overview/xref)

foreach(t caf cancel-tombstone methods)
    inn_tap_test(storage/${t})
endforeach()
foreach(t makehistory sm archive)
    inn_shell_test(storage/${t})
endforeach()

# macOS's gethostbyname() returns hostent data at misaligned addresses,
# which UBSan flags in these two tests (in the test and in the unconverted C
# fallback); turn off only the alignment check for them.
if(INN_SANITIZE)
    foreach(t getaddrinfo getnameinfo)
        target_compile_options(test_lib_${t} PRIVATE -fno-sanitize=alignment)
    endforeach()
endif()
