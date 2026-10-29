# nnrpd — converted from nnrpd/Makefile.  perl.c and python.c compile to
# almost nothing while those embeddings are off.

set(_sasl)
if(HAVE_SASL)
    set(_sasl ${INN_SASL_LIB})
endif()

inn_program(nnrpd
    SOURCES article.c auth-ext.c cache.c commands.c group.c line.c list.c
            misc.c newnews.c nnrpd.c perl.c perm.c post.c python.c sasl.c
            tls.c track.cc zlib.cc
    LIBS ${INN_CANLOCK_LIBS} ${INN_PERL_LIBS} ${INN_PYTHON_LIBS} ${_sasl}
         ZLIB::ZLIB)

set_source_files_properties(perl.c PROPERTIES
    COMPILE_OPTIONS "${INN_PERL_CPPFLAGS}")
set_source_files_properties(python.c PROPERTIES
    COMPILE_OPTIONS "${INN_PYTHON_CPPFLAGS}")
