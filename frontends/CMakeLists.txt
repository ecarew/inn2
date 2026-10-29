# frontends/ programs — converted from frontends/Makefile.  Scripts generated
# from .in files (cnfsstat, mailpost, pullnews, ...) and the optional
# feedone/sys2nf are not built here yet.  The ovdb_* programs build as stubs
# while Berkeley DB support is off.

inn_program(ctlinnd      SOURCES ctlinnd.cc    INNONLY)
inn_program(decode       SOURCES decode.cc     INNONLY)
inn_program(encode       SOURCES encode.cc     NOLIBS)
inn_program(getlist      SOURCES getlist.cc    INNONLY)
inn_program(inews        SOURCES inews.c      INNONLY)
inn_program(innconfval   SOURCES innconfval.cc INNONLY)
inn_program(ovdb_init    SOURCES ovdb_init.c)
inn_program(ovdb_monitor SOURCES ovdb_monitor.c)
inn_program(ovdb_server  SOURCES ovdb_server.c)
inn_program(ovdb_stat    SOURCES ovdb_stat.c)
inn_program(rnews        SOURCES rnews.c)
inn_program(sm           SOURCES sm.cc)
