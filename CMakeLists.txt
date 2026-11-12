cmake_minimum_required(VERSION 4.3)

project(INN VERSION 2.8.0 LANGUAGES C CXX)

# Compile through ccache when it is installed: verification rebuilds several
# build directories for every commit.  A launcher given on the command line
# (-DCMAKE_CXX_COMPILER_LAUNCHER=...) takes precedence.
find_program(INN_CCACHE_PROGRAM ccache)
option(INN_CCACHE "Compile through ccache if it is installed" ON)
if(INN_CCACHE AND INN_CCACHE_PROGRAM)
    foreach(lang C CXX)
        if(NOT CMAKE_${lang}_COMPILER_LAUNCHER)
            set(CMAKE_${lang}_COMPILER_LAUNCHER ${INN_CCACHE_PROGRAM})
        endif()
    endforeach()
endif()

# INN keeps assert() enabled in all builds, as configure's "-g -O2" does, but
# CMake's Release-type defaults add -DNDEBUG.  Strip it.
foreach(lang C CXX)
    foreach(cfg RELEASE RELWITHDEBINFO MINSIZEREL)
        string(REPLACE "-DNDEBUG" "" CMAKE_${lang}_FLAGS_${cfg}
               "${CMAKE_${lang}_FLAGS_${cfg}}")
    endforeach()
endforeach()

# The source generators (storage/buildconfig.in, history/buildconfig.in and
# lib/sqlite-helper-gen.in) are Perl scripts, run straight from their .in
# files: fixscript changes only their #! line.
find_package(Perl REQUIRED)

# inn_sqlite_helper(out_var sql...)
# Generate NAME.cc and NAME.h in the build tree from each NAME.sql (a path
# relative to the current source directory, kept in the output path) with
# sqlite-helper-gen, and return the .cc files in out_var.  The generated
# header is found through the output directory, which the caller adds to the
# target's include path.
function(inn_sqlite_helper out_var)
    set(outputs)
    foreach(sql ${ARGN})
        get_filename_component(dir ${sql} DIRECTORY)
        get_filename_component(name ${sql} NAME_WE)
        set(out ${CMAKE_CURRENT_BINARY_DIR}/${dir})
        add_custom_command(
            OUTPUT ${out}/${name}.cc ${out}/${name}.h
            COMMAND ${CMAKE_COMMAND} -E make_directory ${out}
            COMMAND ${PERL_EXECUTABLE} ${CMAKE_SOURCE_DIR}/lib/sqlite-helper-gen.in
                    -o ${out} ${sql}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            DEPENDS ${sql} ${CMAKE_SOURCE_DIR}/lib/sqlite-helper-gen.in
            COMMENT "Generating ${dir}/${name}.cc from ${sql}"
            VERBATIM)
        list(APPEND outputs ${out}/${name}.cc)
    endforeach()
    set(${out_var} ${outputs} PARENT_SCOPE)
endfunction()

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

# config.h, inn/system.h, inn/paths.h, inn/version.h and inn/portable-*.h
# are generated into the build tree's include directory, first on the
# include path (CMake-only goal item 1; they used to come from ./configure).
# INN_RUNAS_USER/INN_RUNAS_GROUP set RUNASUSER/RUNASGROUP, the account innd
# requires and switches to before it reads inn.conf (configure's
# --with-news-user/--with-news-group).  Used for test builds that run innd
# as the tester (support/live-innd.sh); production builds leave them empty
# for the default, news.
set(INN_RUNAS_USER "" CACHE STRING "Override RUNASUSER (default: news)")
set(INN_RUNAS_GROUP "" CACHE STRING "Override RUNASGROUP (default: news)")
include(cmake/InnConfigure.cmake)
configure_file(include/config.h.cmake.in ${CMAKE_BINARY_DIR}/include/config.h)
add_subdirectory(include)
# Installation directories and inn_fixscript, for generated files.
include(cmake/InnPaths.cmake)
# Install rules (cmake/InnInstall.cmake).  This first install step, before
# any directory installs a file, loads the install-time steps, refuses test
# builds and creates INN's directories; the last one, at the end of this
# file, sets ownership and creates the first-install data.
include(cmake/InnInstall.cmake)
set(_inn_test_build FALSE)
if(INN_RUNAS_USER OR INN_RUNAS_GROUP)
    set(_inn_test_build TRUE)
endif()
install(CODE "
set(INN_I_TEST_BUILD ${_inn_test_build})
set(INN_I_USER \"${INN_NEWS_USER}\")
set(INN_I_GROUP \"${INN_NEWS_GROUP}\")
set(INN_I_BINDIR \"${bindir}\")
set(INN_I_DBDIR \"${DBDIR}\")
set(INN_I_HISTORYDIR \"${HISTORYDIR}\")
set(INN_I_RUNDIR \"${RUNDIR}\")
set(INN_I_DIRS \"${prefix};${bindir};${PATHAUTH};${PATHAUTHRESOLV};${PATHAUTHPASSWD};${CONTROLDIR};${FILTERDIR};${PATHRNEWS};${DBDIR};${docdir};${sysconfdir};${HTTPDIR};${libdir};${LIBPERLDIR};${LIBPERLDIR}/INN;${LIBPERLDIR}/INN/Utils;${datarootdir};${SPOOLDIR};${tmpdir};${SPOOLDIR}/archive;${SPOOLDIR}/articles;${SPOOLDIR}/incoming;${SPOOLDIR}/incoming/bad;${SPOOLDIR}/innfeed;${SPOOLDIR}/overview;${SPOOLDIR}/outgoing;${LOGDIR};${LOGDIR}/OLD;${includedir};${includedir}/inn\")
set(INN_I_DATA_MODE \"${INN_MODE_DATA}\")
set(INN_I_SETGID_INEWS ${INN_SETGID_INEWS})
set(INN_I_ACTIVE_MINIMAL \"${CMAKE_SOURCE_DIR}/samples/active.minimal\")
set(INN_I_NEWSGROUPS_MINIMAL \"${CMAKE_SOURCE_DIR}/samples/newsgroups.minimal\")
include(\"${INN_INSTALL_STEPS}\")
inn_install_begin()
")
add_subdirectory(samples)
include_directories(BEFORE ${CMAKE_BINARY_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(ZLIB REQUIRED)
find_package(SQLite3 REQUIRED)
# Boost is required, header-only libraries so far (CLAUDE.md, "Build
# system"); code that uses it links Boost::headers.  CMake 4 has no
# FindBoost module: this uses Boost's own BoostConfig.cmake.
#
# 1.92 is a minimum, not a pin: Boost 1.92 or any newer release is
# accepted.  Boost's BoostConfigVersion.cmake rejects only versions less
# than the one asked for, so no upper bound or exact match applies.
set(INN_BOOST_MIN_VERSION 1.92)
find_package(Boost ${INN_BOOST_MIN_VERSION} REQUIRED CONFIG)
message(STATUS "Boost ${Boost_VERSION} (need ${INN_BOOST_MIN_VERSION} or newer)")
# Threads::Threads, for buffchan's reader thread (backends/buffchan.cc).
find_package(Threads REQUIRED)

# Optional-feature hooks consumed by innd/CMakeLists.txt and the others.
# Perl and Python come from cmake/InnFilters.cmake (empty unless
# INN_PERL_FILTERS or INN_PYTHON_FILTERS); systemd is not wired in yet.
set(INN_PERL_CPPFLAGS ${INN_FILTER_PERL_CPPFLAGS})
set(INN_PERL_LIBS inn_perl ${INN_FILTER_PERL_LIBS})
set(INN_PYTHON_CPPFLAGS ${INN_FILTER_PYTHON_CPPFLAGS})
set(INN_PYTHON_LIBS ${INN_FILTER_PYTHON_LIBS})
set(INN_SYSTEMD_CFLAGS "")
set(INN_SYSTEMD_LIBS "")
set(INN_CANLOCK_LDFLAGS "")
set(INN_CANLOCK_LIBS inn_canlock)
set(INN_REGEX_LIBS "")
set(INN_EXTRA_LIBS "")

# Optional libraries found by cmake/InnConfigure.cmake (HAVE_PAM,
# HAVE_SASL, HAVE_KRB5, with INN_PAM_LIB, INN_SASL_LIB, INN_KRB5_CFLAGS and
# INN_KRB5_LIBS).

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
# contrib/ is unsupported (contrib/README), so it is built, and its tests are
# registered (tests/CMakeLists.txt), only on request: -DINN_CONTRIB=ON.
option(INN_CONTRIB "Build and test the programs and scripts in contrib/" OFF)
if(INN_CONTRIB)
    add_subdirectory(contrib)
endif()
add_subdirectory(control)
add_subdirectory(expire)
add_subdirectory(frontends)
add_subdirectory(innfeed)
add_subdirectory(nnrpd)
add_subdirectory(perl)
add_subdirectory(scripts)

# INN's tests, run with ctest (tests/CMakeLists.txt).
option(INN_TESTS "Build INN's tests for CTest" ON)
if(INN_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# INN's libraries and headers, for programs built against INN (lib/,
# storage/, history/ and include/'s install targets).  The libraries are
# static, so they are readable files, not executables.
install(TARGETS inn innstorage innhist ARCHIVE DESTINATION ${libdir}
        PERMISSIONS ${INN_MODE_RPUB})
file(GLOB _inn_headers ${CMAKE_SOURCE_DIR}/include/inn/*.h)
install(FILES ${_inn_headers}
        ${CMAKE_BINARY_DIR}/include/inn/paths.h
        ${CMAKE_BINARY_DIR}/include/inn/portable-getaddrinfo.h
        ${CMAKE_BINARY_DIR}/include/inn/portable-getnameinfo.h
        ${CMAKE_BINARY_DIR}/include/inn/portable-macros.h
        ${CMAKE_BINARY_DIR}/include/inn/portable-socket.h
        ${CMAKE_BINARY_DIR}/include/inn/portable-stdbool.h
        ${CMAKE_BINARY_DIR}/include/inn/system.h
        ${CMAKE_BINARY_DIR}/include/inn/version.h
        DESTINATION ${includedir}/inn PERMISSIONS ${INN_MODE_RPUB})

# The last install step (cmake/InnInstallSteps.cmake).
install(CODE "inn_install_end()")
