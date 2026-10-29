# backends/ programs — converted from backends/Makefile.  The shell and Perl
# scripts (actsyncd, mod-active, news2mail, nntpsend, send-*, ...) are
# generated from .in files and are not built here yet.

inn_program(actsync    SOURCES actsync.c    INNONLY)
inn_program(archive    SOURCES archive.c)
inn_program(batcher    SOURCES batcher.c)
inn_program(buffchan   SOURCES buffchan.c   INNONLY)
inn_program(cvtbatch   SOURCES cvtbatch.cc)
inn_program(innbind    SOURCES innbind.c    INNONLY)
inn_program(inndf      SOURCES inndf.c)
inn_program(innxbatch  SOURCES innxbatch.c  INNONLY)
inn_program(innxmit    SOURCES innxmit.c)
inn_program(ninpaths   SOURCES ninpaths.c   NOLIBS)
inn_program(nntpget    SOURCES nntpget.c)
inn_program(overchan   SOURCES overchan.c)
inn_program(shlock     SOURCES shlock.c     INNONLY)
inn_program(shrinkfile SOURCES shrinkfile.c INNONLY)
