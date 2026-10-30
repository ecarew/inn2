# libinnhist — history database.

# Generated sources, written to the build tree: buildconfig scans the method
# subdirectories' hismethod.config files for the history method table
# (hismethods.cc/.h; it also writes Make.methods for the Make build, unused
# here), and hissqlite's SQL becomes C++ through sqlite-helper-gen (root
# CMakeLists.txt).
file(GLOB history_method_configs CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*/hismethod.config)
add_custom_command(
    OUTPUT hismethods.cc hismethods.h Make.methods
    COMMAND ${PERL_EXECUTABLE} buildconfig.in ${CMAKE_CURRENT_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS buildconfig.in ${history_method_configs}
    COMMENT "Generating the history method table"
    VERBATIM)
inn_sqlite_helper(hissqlite_sources
    hissqlite/hissqlite-init.sql hissqlite/hissqlite-main.sql
    hissqlite/hissqlite-read.sql)

add_library(innhist STATIC
    his.cc ${CMAKE_CURRENT_BINARY_DIR}/hismethods.cc
    ${hissqlite_sources} hissqlite/hissqlite.cc
    hisv6/hisv6.cc
)
target_include_directories(innhist PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/hissqlite)
target_link_libraries(innhist PUBLIC INN::inn SQLite3::SQLite3)
# hisv6 uses libinnstorage's token functions (IsToken, TextToToken,
# TokenToText), and libinnstorage's expire.cc uses HISlookup: the two
# libraries depend on each other.  Declaring both directions lets CMake repeat
# them on the link line, which GNU ld needs for a cycle between static
# libraries (Apple's ld resolves them in any order).
target_link_libraries(innhist PUBLIC innstorage)
add_library(INN::hist ALIAS innhist)

# History tools (history/Make.methods).  hissqlite-util is a script generated
# from hissqlite-util.in and is not built here yet.
inn_program(hissqlite-convert SOURCES hissqlite/hissqlite-convert.cc)
