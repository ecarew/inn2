# libinn — general support library.
# setproctitle.c, sd-daemon.cc and reallocarray.cc are the replacement functions
# configure selected (LIBOBJS) for macOS; other platforms may need others.

add_library(inn STATIC
    setproctitle.cc sd-daemon.cc reallocarray.cc
    argparse.cc artnumber.cc bloom.cc buffer.cc cleanfrom.cc clientactive.cc
    clientlib.cc commands.cc concat.cc conffile.cc confparse.c date.c dbz.c
    defdist.cc dispatch.cc fdflag.cc fdlimit.cc getfqdn.cc getmodaddr.cc hash.cc
    hashtab.c headers.cc hex.cc innconf.c inndcomm.cc list.cc localopen.cc
    lockfile.cc makedir.cc md5.c messageid.cc messages.cc mmap.cc network.c
    network-innbind.cc newsuser.cc nntp.cc qio.cc radix32.cc readin.cc remopen.cc
    reservedfd.cc resource.cc secrets.cc sendarticle.cc sendpass.cc sequence.cc
    sqlite-helper.cc timer.cc tst.cc uwildmat.cc vector.c wire.cc xfopena.cc
    xmalloc.cc xsignal.cc xwrite.cc
)
target_include_directories(inn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inn PUBLIC SQLite::SQLite3)
add_library(INN::inn ALIAS inn)

# Linked directly into programs rather than libinn, as the Make build does
# via CANLOCK_LIBS and PERL_LIBS.  Both compile to stubs when the feature is off.
add_library(inn_canlock OBJECT canlock.cc)
target_include_directories(inn_canlock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(inn_perl OBJECT perl.c)
target_include_directories(inn_perl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
