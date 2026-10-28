# libinnhist — history database.  hismethods.c and hissqlite/*.c (except
# hissqlite.c) are generated; see the root CMakeLists.txt.

add_library(innhist STATIC
    his.c hismethods.c
    hissqlite/hissqlite-init.c hissqlite/hissqlite-main.c
    hissqlite/hissqlite-read.c hissqlite/hissqlite.c
    hisv6/hisv6.c
)
target_include_directories(innhist PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(innhist PUBLIC INN::inn SQLite::SQLite3)
add_library(INN::hist ALIAS innhist)
