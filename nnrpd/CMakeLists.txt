# nnrpd — converted from nnrpd/Makefile.  perl.c and python.c compile to
# almost nothing while those embeddings are off.

set(_sasl)
if(HAVE_SASL)
    set(_sasl ${INN_SASL_LIB})
endif()

inn_program(nnrpd
    SOURCES article.c auth-ext.cc cache.cc commands.c group.cc line.cc list.cc
            misc.c newnews.cc nnrpd.c perl.c perm.c post.c python.c sasl.cc
            tls.c track.cc zlib.cc
    LIBS ${INN_CANLOCK_LIBS} ${INN_PERL_LIBS} ${INN_PYTHON_LIBS} ${_sasl}
         ZLIB::ZLIB)

set_source_files_properties(perl.c PROPERTIES
    COMPILE_OPTIONS "${INN_PERL_CPPFLAGS}")
set_source_files_properties(python.c PROPERTIES
    COMPILE_OPTIONS "${INN_PYTHON_CPPFLAGS}")
