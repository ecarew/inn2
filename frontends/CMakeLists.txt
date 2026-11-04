# frontends/ programs — converted from frontends/Makefile.  The scripts are
# generated from their .in files (inn_scripts, cmake/InnPaths.cmake).
# The ovdb_* programs build as stubs while Berkeley DB support is off.

inn_program(ctlinnd      SOURCES ctlinnd.cc    INNONLY)
inn_program(getlist      SOURCES getlist.cc    INNONLY)
inn_program(inews        SOURCES inews.cc      INNONLY)
inn_program(innconfval   SOURCES innconfval.cc INNONLY)
inn_program(ovdb_init    SOURCES ovdb_init.cc)
inn_program(ovdb_monitor SOURCES ovdb_monitor.cc)
inn_program(ovdb_server  SOURCES ovdb_server.cc)
inn_program(ovdb_stat    SOURCES ovdb_stat.cc)
inn_program(rnews        SOURCES rnews.cc)
inn_program(sm           SOURCES sm.cc)

# feedone and sys2nf: built only on request, like Make's "make optional".
option(INN_OPTIONAL_PROGRAMS "Build the optional frontends feedone and sys2nf" OFF)
if(INN_OPTIONAL_PROGRAMS)
    inn_program(feedone SOURCES feedone.cc INNONLY)
    inn_program(sys2nf  SOURCES sys2nf.cc  INNONLY)
endif()

inn_scripts(cnfsheadconf cnfsstat mailpost pullnews scanspool)

# bunbatch and gunbatch, the decompressors rnews runs from rnews.libexec for
# "#! bunbatch" and "#! gunbatch" batches (frontends/Makefile writes them).
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bunbatch "#! ${SHELL}\nexec ${BZIP2} -d -c\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gunbatch "#! ${SHELL}\nexec ${GZIP} -d -c\n")

# frontends/Makefile's install target.  inews and rnews have their own
# modes (INEWSMODE, RNEWSMODE); feedone and sys2nf are not installed.
inn_install(TARGETS inews DESTINATION ${bindir} MODE INEWS)
inn_install(TARGETS rnews DESTINATION ${bindir} MODE RNEWS)
inn_install(FILES cnfsheadconf DESTINATION ${bindir} MODE XPRI)
inn_install(FILES cnfsstat mailpost pullnews scanspool DESTINATION ${bindir}
            MODE XPUB)
inn_install(TARGETS ctlinnd ovdb_init ovdb_monitor ovdb_server ovdb_stat
            DESTINATION ${bindir} MODE XPRI)
inn_install(TARGETS getlist innconfval sm DESTINATION ${bindir} MODE XPUB)
inn_install(FILES bunbatch gunbatch DESTINATION ${PATHRNEWS} MODE XPUB)
