# libinnhist — history database.  hismethods.c and hissqlite/*.c (except
# hissqlite.c) are generated; see the root CMakeLists.txt.

add_library(innhist STATIC
    his.cc hismethods.c
    hissqlite/hissqlite-init.c hissqlite/hissqlite-main.c
    hissqlite/hissqlite-read.c hissqlite/hissqlite.cc
    hisv6/hisv6.cc
)
target_include_directories(innhist PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(innhist PUBLIC INN::inn SQLite3::SQLite3)
add_library(INN::hist ALIAS innhist)

# History tools (history/Make.methods).  hissqlite-util is a script generated
# from hissqlite-util.in and is not built here yet.
inn_program(hissqlite-convert SOURCES hissqlite/hissqlite-convert.c)
