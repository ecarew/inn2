# libinnstorage — article storage and overview methods.  methods.c,
# ovmethods.c and ovsqlite/sql-*.c are generated; see the root CMakeLists.txt.

add_library(innstorage STATIC
    expire.cc interface.cc methods.c ov.cc overdata.cc overview.cc ovmethods.c
    tombstone.cc
    buffindexed/buffindexed.cc buffindexed/shmem.cc
    cnfs/cnfs.cc
    ovdb/ovdb.cc
    ovsqlite/ovsqlite-private.cc ovsqlite/ovsqlite.cc ovsqlite/sql-read.c
    timecaf/caf.cc timecaf/timecaf.cc
    timehash/timehash.cc
    tradindexed/tdx-cache.cc tradindexed/tdx-data.cc tradindexed/tdx-group.cc
    tradindexed/tradindexed.cc
    tradspool/tradspool.cc
    trash/trash.cc
)
target_include_directories(innstorage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(innstorage PUBLIC INN::inn ZLIB::ZLIB SQLite3::SQLite3)
# expire.cc uses HISlookup from libinnhist, which in turn uses this
# library's token functions; see history/CMakeLists.txt.
target_link_libraries(innstorage PUBLIC innhist)
add_library(INN::storage ALIAS innstorage)

# innd/CMakeLists.txt links this for the libraries libinnstorage needs.
add_library(storage_deps INTERFACE)
target_link_libraries(storage_deps INTERFACE ZLIB::ZLIB SQLite3::SQLite3)
add_library(INN::storage_deps ALIAS storage_deps)

# Storage tools (storage/Make.methods).  ovsqlite-util is a script generated
# from ovsqlite-util.in and is not built here yet.
inn_program(tdx-util SOURCES tradindexed/tdx-util.cc)
inn_program(ovsqlite-server
    SOURCES ovsqlite/ovsqlite-server.cc ovsqlite/sql-main.c ovsqlite/sql-init.c
            ovsqlite/ovsqlite-private.cc)
# buffindexed.cc again, with its BUFF_DEBUG main().
inn_program(buffindexed_d SOURCES buffindexed/buffindexed.cc)
target_compile_definitions(buffindexed_d PRIVATE BUFF_DEBUG)
target_compile_features(buffindexed_d PRIVATE cxx_std_20)
