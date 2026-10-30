# libinn — general support library.

# Replacement functions, compiled only where the system lacks the original
# (configure's AC_REPLACE_FUNCS and LIBOBJS; the checks are in
# cmake/InnConfigure.cmake).  sd-daemon.cc stands in for libsystemd, which
# the CMake build does not use yet (HAVE_SD_NOTIFY).
set(replacements)
foreach(func asprintf daemon getaddrinfo getnameinfo getpagesize inet_aton
        inet_ntoa inet_ntop mkstemp pread pwrite reallocarray setenv seteuid
        setproctitle snprintf strcasecmp strlcat strlcpy strspn strtok symlink)
    string(TOUPPER "HAVE_${func}" have)
    if(NOT ${have})
        list(APPEND replacements ${func}.cc)
    endif()
endforeach()
if(NOT HAVE_SD_NOTIFY)
    list(APPEND replacements sd-daemon.cc)
endif()
message(STATUS "libinn replacement functions: ${replacements}")

add_library(inn STATIC
    ${replacements}
    argparse.cc artnumber.cc bloom.cc buffer.cc cleanfrom.cc clientactive.cc
    clientlib.cc commands.cc concat.cc conffile.cc confparse.cc date.cc dbz.cc
    defdist.cc dispatch.cc fdflag.cc fdlimit.cc getfqdn.cc getmodaddr.cc hash.cc
    hashtab.cc headers.cc hex.cc innconf.cc inndcomm.cc list.cc localopen.cc
    lockfile.cc makedir.cc md5.cc messageid.cc messages.cc mmap.cc network.cc
    network-innbind.cc newsuser.cc nntp.cc qio.cc radix32.cc readin.cc remopen.cc
    reservedfd.cc resource.cc secrets.cc sendarticle.cc sendpass.cc sequence.cc
    sqlite-helper.cc timer.cc tst.cc uwildmat.cc vector.cc wire.cc xfopena.cc
    xmalloc.cc xsignal.cc xwrite.cc
)
target_include_directories(inn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inn PUBLIC SQLite3::SQLite3)
add_library(INN::inn ALIAS inn)

# Linked directly into programs rather than libinn, as the Make build does
# via CANLOCK_LIBS and PERL_LIBS.  Both compile to stubs when the feature is off.
add_library(inn_canlock OBJECT canlock.cc)
target_include_directories(inn_canlock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(inn_perl OBJECT perl.cc)
target_include_directories(inn_perl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
