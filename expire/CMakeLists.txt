# expire/ programs — converted from expire/Makefile.  expirerm is a script
# generated from expirerm.in (inn_scripts, cmake/InnPaths.cmake).

inn_program(convdate     SOURCES convdate.cc     INNONLY)
inn_program(expire       SOURCES expire.cc)
inn_program(expireover   SOURCES expireover.cc)
inn_program(fastrm       SOURCES fastrm.cc)
inn_program(grephistory  SOURCES grephistory.cc)
inn_program(makedbz      SOURCES makedbz.cc      INNONLY)
inn_program(makehistory  SOURCES makehistory.cc)
inn_program(prunehistory SOURCES prunehistory.cc)

inn_scripts(expirerm)
