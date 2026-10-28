# libinnstorage — article storage and overview methods.  methods.c,
# ovmethods.c and ovsqlite/sql-*.c are generated; see the root CMakeLists.txt.

add_library(innstorage STATIC
    expire.cc interface.cc methods.c ov.cc overdata.cc overview.cc ovmethods.c
    tombstone.cc
    buffindexed/buffindexed.c buffindexed/shmem.cc
    cnfs/cnfs.c
    ovdb/ovdb.c
    ovsqlite/ovsqlite-private.cc ovsqlite/ovsqlite.c ovsqlite/sql-read.c
    timecaf/caf.cc timecaf/timecaf.cc
    timehash/timehash.cc
    tradindexed/tdx-cache.cc tradindexed/tdx-data.cc tradindexed/tdx-group.cc
    tradindexed/tradindexed.cc
    tradspool/tradspool.cc
    trash/trash.cc
)
target_include_directories(innstorage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(innstorage PUBLIC INN::inn ZLIB::ZLIB SQLite::SQLite3)
add_library(INN::storage ALIAS innstorage)

# innd/CMakeLists.txt links this for the libraries libinnstorage needs.
add_library(storage_deps INTERFACE)
target_link_libraries(storage_deps INTERFACE ZLIB::ZLIB SQLite::SQLite3)
add_library(INN::storage_deps ALIAS storage_deps)
