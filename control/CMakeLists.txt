# control/ programs — converted from control/Makefile.  The Perl scripts
# are generated from their .in files (inn_scripts, cmake/InnPaths.cmake).

inn_program(gencancel SOURCES gencancel.cc LIBS ${INN_CANLOCK_LIBS})

inn_scripts(controlbatch controlchan docheckgroups perl-nocem pgpverify)
