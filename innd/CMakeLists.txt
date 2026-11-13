# innd and tinyleaf — converted from innd/Makefile.  INN::* targets and the
# INN_* feature variables come from the root CMakeLists.txt.

# ── innd ──────────────────────────────────────────────────────────────────────

# innd_core is everything except innd.cc (main() and innd's global data), so
# the innd tests can link it with tests/innd/fakeinnd.c in place of innd.cc,
# as tests/Makefile does with INNOBJS.
add_library(innd_core STATIC
    art.cc
    wip.cc
    util.cc
    status.cc
    keywords.cc
    lc.cc
    nc.cc
    ng.cc
    proc.cc
    newsfeeds.cc
    rc.cc
    site.cc
    icd.cc
    chan.cc
    cc.cc
    perl.cc
    python.cc
)
target_compile_features(innd_core PUBLIC cxx_std_20)
set_target_properties(innd_core PROPERTIES CXX_EXTENSIONS OFF)

add_executable(innd innd.cc)
set_target_properties(innd PROPERTIES CXX_EXTENSIONS OFF)
# Whole archive, so innd keeps every object as the Make build linked them
# (some, like KEYgenerate and the canlock stubs, are not referenced).
target_link_libraries(innd PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,innd_core>")

# perl.cc and python.cc need extra include flags
set_source_files_properties(perl.cc PROPERTIES
    COMPILE_OPTIONS "${INN_PERL_CPPFLAGS}"
)
set_source_files_properties(python.cc PROPERTIES
    COMPILE_OPTIONS "${INN_PYTHON_CPPFLAGS}"
)

target_compile_options(innd_core PUBLIC
    ${INN_SYSTEMD_CFLAGS}
)

target_link_libraries(innd_core PUBLIC
    Boost::headers
    Threads::Threads
    INN::storage
    INN::hist
    INN::inn
    INN::storage_deps
    ${INN_SYSTEMD_LIBS}
    ${INN_CANLOCK_LDFLAGS}
    ${INN_CANLOCK_LIBS}
    ${INN_PERL_LIBS}
    ${INN_PYTHON_LIBS}
    ${INN_REGEX_LIBS}
    ${INN_EXTRA_LIBS}
)

# ── tinyleaf ──────────────────────────────────────────────────────────────────

add_executable(tinyleaf tinyleaf.cc)

target_compile_features(tinyleaf PRIVATE cxx_std_20)
set_target_properties(tinyleaf PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(tinyleaf PRIVATE
    INN::inn
    ${INN_EXTRA_LIBS}
)

# ── install ───────────────────────────────────────────────────────────────────

# innd/Makefile's install target: innd 0550 (LI_XPRI), tinyleaf 0555
# (LI_XPUB), owned by news (cmake/InnInstall.cmake).
inn_install(TARGETS innd     DESTINATION ${bindir} MODE XPRI)
inn_install(TARGETS tinyleaf DESTINATION ${bindir} MODE XPUB)
