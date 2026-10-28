# innd and tinyleaf — converted from innd/Makefile.  INN::* targets and the
# INN_* feature variables come from the root CMakeLists.txt.

# ── innd ──────────────────────────────────────────────────────────────────────

set(INND_C_SOURCES
    cc.c
    chan.c
    icd.c
    innd.c
    keywords.c
    lc.c
    nc.c
    newsfeeds.c
    ng.c
    perl.c
    proc.c
    python.c
    rc.c
    site.c
    status.c
    util.c
)

set(INND_CXX_SOURCES
    art.cc
    wip.cc
)

add_executable(innd ${INND_C_SOURCES} ${INND_CXX_SOURCES})

target_compile_features(innd PRIVATE cxx_std_20)
set_target_properties(innd PROPERTIES CXX_EXTENSIONS OFF)

# perl.c and python.c need extra include flags
set_source_files_properties(perl.c PROPERTIES
    COMPILE_OPTIONS "${INN_PERL_CPPFLAGS}"
)
set_source_files_properties(python.c PROPERTIES
    COMPILE_OPTIONS "${INN_PYTHON_CPPFLAGS}"
)

target_compile_options(innd PRIVATE
    ${INN_SYSTEMD_CFLAGS}
)

target_link_libraries(innd PRIVATE
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

add_executable(tinyleaf tinyleaf.c)

target_compile_features(tinyleaf PRIVATE cxx_std_20)
set_target_properties(tinyleaf PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(tinyleaf PRIVATE
    INN::inn
    ${INN_EXTRA_LIBS}
)

# ── install ───────────────────────────────────────────────────────────────────

install(TARGETS innd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS
        OWNER_READ OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        SETUID   # corresponds to LI_XPRI (-m 0550 with news owner)
)

install(TARGETS tinyleaf
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS
        OWNER_READ OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE  # corresponds to LI_XPUB (-m 0555)
)
