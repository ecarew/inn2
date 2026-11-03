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
