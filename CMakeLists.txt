cmake_minimum_required(VERSION 4.3)

project(INN VERSION 2.8.0 LANGUAGES C CXX)

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
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Hardening flags matching the Make build (configure default).
add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
add_compile_options(-fstack-protector-strong)
add_link_options(-fstack-protector-strong)

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

add_subdirectory(lib)
add_subdirectory(history)
add_subdirectory(storage)
add_subdirectory(innd)
