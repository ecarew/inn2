cmake_minimum_required(VERSION 4.3)

project(INN VERSION 2.8.0 LANGUAGES C CXX)

# INN keeps assert() enabled in all builds, as configure's "-g -O2" does, but
# CMake's Release-type defaults add -DNDEBUG.  Strip it.
foreach(lang C CXX)
    foreach(cfg RELEASE RELWITHDEBINFO MINSIZEREL)
        string(REPLACE "-DNDEBUG" "" CMAKE_${lang}_FLAGS_${cfg}
               "${CMAKE_${lang}_FLAGS_${cfg}}")
    endforeach()
endforeach()

# Phase 1: reuse the headers and method tables produced by ./configure and the
# Perl generators (storage/buildconfig, history/buildconfig, sqlite-helper-gen)
# instead of reproducing configure.ac's checks in CMake.
foreach(generated
        include/config.h
        include/inn/system.h
        include/inn/paths.h
        storage/methods.c
        storage/ovmethods.c
        history/hismethods.c
        storage/ovsqlite/sql-read.c
        history/hissqlite/hissqlite-main.c)
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/${generated})
        message(FATAL_ERROR
            "${generated} is missing. Run ./configure, then "
            "'make -C include && make -C lib && make -C storage && make -C history' "
            "once to produce the generated sources.")
    endif()
endforeach()

include(GNUInstallDirs)

# compile_commands.json for clang-tidy and other tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(INN_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(INN_SANITIZE)
    set(INN_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer
        -fno-sanitize-recover=undefined)
    add_compile_options(${INN_SANITIZE_FLAGS})
    add_link_options(${INN_SANITIZE_FLAGS})
endif()

# Source-based code coverage; see support/coverage-report.sh.
option(INN_COVERAGE "Build with clang source-based code coverage" OFF)
if(INN_COVERAGE)
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()

set(CMAKE_C_STANDARD 11)
# Every C++ target is C++20 (CLAUDE.md, "Build system"), without GNU
# extensions.  Set once here: per-target settings had left most libraries on
# the compiler default (C++14 for Apple Clang 21).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Hardening flags matching the Make build (configure default).
add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
add_compile_options(-fstack-protector-strong)
add_link_options(-fstack-protector-strong)

# CMake's own copy of configure's config.h, first on the include path, so
# build options can override values in it.  INN_RUNAS_USER/INN_RUNAS_GROUP
# replace RUNASUSER/RUNASGROUP, the account innd requires and switches to
# before it reads inn.conf (configure's --with-news-user/--with-news-group).
# Used for test builds that run innd as the tester (support/live-innd.sh);
# production builds leave them empty and keep configure's value (news).
set(INN_RUNAS_USER "" CACHE STRING "Override RUNASUSER (default: configure's)")
set(INN_RUNAS_GROUP "" CACHE STRING "Override RUNASGROUP (default: configure's)")

file(READ ${CMAKE_SOURCE_DIR}/include/config.h _config_h)
if(INN_RUNAS_USER)
    string(REGEX REPLACE "#define RUNASUSER \"[^\"]*\""
           "#define RUNASUSER \"${INN_RUNAS_USER}\"" _config_h "${_config_h}")
endif()
if(INN_RUNAS_GROUP)
    string(REGEX REPLACE "#define RUNASGROUP \"[^\"]*\""
           "#define RUNASGROUP \"${INN_RUNAS_GROUP}\"" _config_h "${_config_h}")
endif()
file(WRITE ${CMAKE_BINARY_DIR}/config.h.new "${_config_h}")
configure_file(${CMAKE_BINARY_DIR}/config.h.new
               ${CMAKE_BINARY_DIR}/include/config.h COPYONLY)
include_directories(BEFORE ${CMAKE_BINARY_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(ZLIB REQUIRED)
find_package(SQLite3 REQUIRED)

# Optional-feature hooks consumed by innd/CMakeLists.txt; empty until those
# features (Perl, Python, systemd, canlock) are wired into CMake.
set(INN_PERL_CPPFLAGS "")
set(INN_PERL_LIBS inn_perl)
set(INN_PYTHON_CPPFLAGS "")
set(INN_PYTHON_LIBS "")
set(INN_SYSTEMD_CFLAGS "")
set(INN_SYSTEMD_LIBS "")
set(INN_CANLOCK_LDFLAGS "")
set(INN_CANLOCK_LIBS inn_canlock)
set(INN_REGEX_LIBS "")
set(INN_EXTRA_LIBS "")

# Optional libraries that config.h (from ./configure) enabled.  Phase 1
# mirrors configure's choices rather than re-detecting features.
file(READ ${CMAKE_SOURCE_DIR}/include/config.h INN_CONFIG_H)
macro(inn_config_has var)
    string(REGEX MATCH "#define ${var} 1" _m "${INN_CONFIG_H}")
    if(_m)
        set(${var} TRUE)
    else()
        set(${var} FALSE)
    endif()
endmacro()
inn_config_has(HAVE_PAM)
inn_config_has(HAVE_SASL)
inn_config_has(HAVE_KRB5)

if(HAVE_PAM)
    find_library(INN_PAM_LIB pam REQUIRED)
endif()
if(HAVE_SASL)
    find_library(INN_SASL_LIB sasl2 REQUIRED)
endif()
if(HAVE_KRB5)
    # configure used krb5-config; use the same one so flags match config.h.
    find_program(INN_KRB5_CONFIG krb5-config REQUIRED)
    execute_process(COMMAND ${INN_KRB5_CONFIG} --cflags krb5
                    OUTPUT_VARIABLE INN_KRB5_CFLAGS
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${INN_KRB5_CONFIG} --libs krb5
                    OUTPUT_VARIABLE INN_KRB5_LIBS
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    separate_arguments(INN_KRB5_CFLAGS)
    separate_arguments(INN_KRB5_LIBS)
endif()

# inn_program(name SOURCES src... [LIBS lib...] [NOLIBS])
# A program linked against all three INN libraries (or only LIBS / none).
function(inn_program name)
    cmake_parse_arguments(P "NOLIBS;INNONLY" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${P_SOURCES})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(P_INNONLY)
        target_link_libraries(${name} PRIVATE INN::inn ${P_LIBS})
    elseif(NOT P_NOLIBS)
        target_link_libraries(${name} PRIVATE INN::storage INN::hist INN::inn
                              ${P_LIBS})
    else()
        target_link_libraries(${name} PRIVATE ${P_LIBS})
    endif()
endfunction()

add_subdirectory(lib)
add_subdirectory(history)
add_subdirectory(storage)
add_subdirectory(innd)
add_subdirectory(authprogs)
add_subdirectory(backends)
add_subdirectory(control)
add_subdirectory(expire)
add_subdirectory(frontends)
add_subdirectory(innfeed)
add_subdirectory(nnrpd)
