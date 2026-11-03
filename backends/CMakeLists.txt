# backends/ programs — converted from backends/Makefile.  The shell and Perl
# scripts are generated from their .in files (inn_scripts,
# cmake/InnPaths.cmake).

inn_program(actsync    SOURCES actsync.cc    INNONLY)
inn_program(archive    SOURCES archive.cc)
inn_program(batcher    SOURCES batcher.cc)
inn_program(buffchan   SOURCES buffchan.cc   INNONLY)
inn_program(cvtbatch   SOURCES cvtbatch.cc)
inn_program(innbind    SOURCES innbind.cc    INNONLY)
inn_program(inndf      SOURCES inndf.cc)
inn_program(innxbatch  SOURCES innxbatch.cc  INNONLY)
inn_program(innxmit    SOURCES innxmit.cc)
inn_program(ninpaths   SOURCES ninpaths.cc   NOLIBS)
inn_program(nntpget    SOURCES nntpget.cc)
inn_program(overchan   SOURCES overchan.cc)
inn_program(shlock     SOURCES shlock.cc     INNONLY)
inn_program(shrinkfile SOURCES shrinkfile.cc INNONLY)

inn_scripts(actmerge actsyncd mod-active news2mail nntpsend send-ihave
            sendinpaths sendxbatches)
inn_scripts(delayer NOLIB)
