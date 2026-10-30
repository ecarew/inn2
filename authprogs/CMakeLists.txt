# authprogs/ programs — converted from authprogs/Makefile.  Each links the
# small libinnauth helper object as well as libinn.

add_library(innauth OBJECT libinnauth.cc)
target_include_directories(innauth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set(_pam)
if(HAVE_PAM)
    set(_pam ${INN_PAM_LIB})
endif()

# crypt() is in libc on macOS and the BSDs but in libcrypt on glibc systems,
# as configure's INN_SEARCH_AUX_LIBS([crypt], [crypt], [CRYPT_LIBS]) finds.
include(CheckFunctionExists)
include(CheckLibraryExists)
check_function_exists(crypt INN_CRYPT_IN_LIBC)
set(_crypt)
if(NOT INN_CRYPT_IN_LIBC)
    check_library_exists(crypt crypt "" INN_HAVE_LIBCRYPT)
    if(INN_HAVE_LIBCRYPT)
        set(_crypt crypt)
    endif()
endif()

# dbm_open: in libc on macOS, in a dbm library on glibc systems
# (INN_DBM_LIBS, from cmake/InnConfigure.cmake).
inn_program(ckpasswd SOURCES ckpasswd.cc INNONLY
            LIBS innauth ${_pam} ${_crypt} ${INN_DBM_LIBS})
inn_program(domain   SOURCES domain.cc   INNONLY LIBS innauth)
inn_program(ident    SOURCES ident.cc    INNONLY LIBS innauth)
inn_program(radius   SOURCES radius.cc   INNONLY LIBS innauth)

if(HAVE_KRB5)
    inn_program(auth_krb5 SOURCES auth_krb5.c INNONLY
                LIBS innauth ${INN_KRB5_LIBS})
    target_compile_options(auth_krb5 PRIVATE ${INN_KRB5_CFLAGS})
endif()
