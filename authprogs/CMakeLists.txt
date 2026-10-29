# authprogs/ programs — converted from authprogs/Makefile.  Each links the
# small libinnauth helper object as well as libinn.

add_library(innauth OBJECT libinnauth.cc)
target_include_directories(innauth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set(_pam)
if(HAVE_PAM)
    set(_pam ${INN_PAM_LIB})
endif()

inn_program(ckpasswd SOURCES ckpasswd.cc INNONLY LIBS innauth ${_pam})
inn_program(domain   SOURCES domain.cc   INNONLY LIBS innauth)
inn_program(ident    SOURCES ident.cc    INNONLY LIBS innauth)
inn_program(radius   SOURCES radius.c   INNONLY LIBS innauth)

if(HAVE_KRB5)
    inn_program(auth_krb5 SOURCES auth_krb5.c INNONLY
                LIBS innauth ${INN_KRB5_LIBS})
    target_compile_options(auth_krb5 PRIVATE ${INN_KRB5_CFLAGS})
endif()
