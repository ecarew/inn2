# libinnstorage — article storage and overview methods.  methods.c,
# ovmethods.c and ovsqlite/sql-*.c are generated; see the root CMakeLists.txt.

add_library(innstorage STATIC
    expire.c interface.c methods.c ov.cc overdata.cc overview.cc ovmethods.c
    tombstone.cc
    buffindexed/buffindexed.c buffindexed/shmem.c
    cnfs/cnfs.c
    ovdb/ovdb.c
    ovsqlite/ovsqlite-private.cc ovsqlite/ovsqlite.c ovsqlite/sql-read.c
    timecaf/caf.c timecaf/timecaf.c
    timehash/timehash.c
    tradindexed/tdx-cache.c tradindexed/tdx-data.c tradindexed/tdx-group.c
    tradindexed/tradindexed.c
    tradspool/tradspool.c
    trash/trash.cc
)
target_include_directories(innstorage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(innstorage PUBLIC INN::inn ZLIB::ZLIB SQLite::SQLite3)
add_library(INN::storage ALIAS innstorage)

# innd/CMakeLists.txt links this for the libraries libinnstorage needs.
add_library(storage_deps INTERFACE)
target_link_libraries(storage_deps INTERFACE ZLIB::ZLIB SQLite::SQLite3)
add_library(INN::storage_deps ALIAS storage_deps)
