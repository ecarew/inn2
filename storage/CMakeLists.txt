# libinnstorage — article storage and overview methods.

# Generated sources, written to the build tree.  buildconfig scans the
# method subdirectories' method.config and ovmethod.config files for the
# storage and overview method tables (methods.cc/.h, ovmethods.cc/.h); it
# also writes Make.methods for the Make build, unused here.  ovsqlite's SQL
# becomes C++ through sqlite-helper-gen (root CMakeLists.txt).
file(GLOB storage_method_configs CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*/method.config
    ${CMAKE_CURRENT_SOURCE_DIR}/*/ovmethod.config)
add_custom_command(
    OUTPUT methods.cc methods.h ovmethods.cc ovmethods.h Make.methods
    COMMAND ${PERL_EXECUTABLE} buildconfig.in ${CMAKE_CURRENT_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS buildconfig.in ${storage_method_configs}
    COMMENT "Generating the storage and overview method tables"
    VERBATIM)
inn_sqlite_helper(ovsqlite_read_sources ovsqlite/sql-read.sql)
inn_sqlite_helper(ovsqlite_server_sources
    ovsqlite/sql-main.sql ovsqlite/sql-init.sql)

add_library(innstorage STATIC
    expire.cc interface.cc ${CMAKE_CURRENT_BINARY_DIR}/methods.cc ov.cc
    overdata.cc overview.cc ${CMAKE_CURRENT_BINARY_DIR}/ovmethods.cc
    tombstone.cc
    buffindexed/buffindexed.cc buffindexed/shmem.cc
    cnfs/cnfs.cc
    ovdb/ovdb.cc
    ovsqlite/ovsqlite-private.cc ovsqlite/ovsqlite.cc ${ovsqlite_read_sources}
    timecaf/caf.cc timecaf/timecaf.cc
    timehash/timehash.cc
    tradindexed/tdx-cache.cc tradindexed/tdx-data.cc tradindexed/tdx-group.cc
    tradindexed/tradindexed.cc
    tradspool/tradspool.cc
    trash/trash.cc
)
target_include_directories(innstorage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/ovsqlite)
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
    SOURCES ovsqlite/ovsqlite-server.cc ${ovsqlite_server_sources}
            ovsqlite/ovsqlite-private.cc)
target_include_directories(ovsqlite-server PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/ovsqlite)
# buffindexed.cc again, with its BUFF_DEBUG main().
inn_program(buffindexed_d SOURCES buffindexed/buffindexed.cc)
target_compile_definitions(buffindexed_d PRIVATE BUFF_DEBUG)
target_compile_features(buffindexed_d PRIVATE cxx_std_20)
