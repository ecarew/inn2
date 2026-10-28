# libinn — general support library.
# setproctitle.c, sd-daemon.cc and reallocarray.c are the replacement functions
# configure selected (LIBOBJS) for macOS; other platforms may need others.

add_library(inn STATIC
    setproctitle.c sd-daemon.cc reallocarray.c
    argparse.c artnumber.c bloom.c buffer.c cleanfrom.c clientactive.c
    clientlib.c commands.cc concat.c conffile.c confparse.c date.c dbz.c
    defdist.c dispatch.cc fdflag.c fdlimit.c getfqdn.c getmodaddr.c hash.c
    hashtab.c headers.c hex.c innconf.c inndcomm.c list.c localopen.c
    lockfile.cc makedir.cc md5.c messageid.c messages.c mmap.cc network.c
    network-innbind.c newsuser.c nntp.c qio.c radix32.c readin.c remopen.c
    reservedfd.c resource.cc secrets.c sendarticle.cc sendpass.c sequence.cc
    sqlite-helper.c timer.c tst.c uwildmat.c vector.c wire.c xfopena.cc
    xmalloc.c xsignal.c xwrite.c
)
target_include_directories(inn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inn PUBLIC SQLite::SQLite3)
add_library(INN::inn ALIAS inn)

# Linked directly into programs rather than libinn, as the Make build does
# via CANLOCK_LIBS and PERL_LIBS.  Both compile to stubs when the feature is off.
add_library(inn_canlock OBJECT canlock.c)
target_include_directories(inn_canlock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(inn_perl OBJECT perl.c)
target_include_directories(inn_perl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
