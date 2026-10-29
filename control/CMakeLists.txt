# control/ programs — converted from control/Makefile.  The Perl scripts
# (controlchan, pgpverify, ...) are generated from .in files and are not built
# here yet.

inn_program(gencancel SOURCES gencancel.cc LIBS ${INN_CANLOCK_LIBS})
