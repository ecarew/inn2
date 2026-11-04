# control/ programs — converted from control/Makefile.  The Perl scripts
# are generated from their .in files (inn_scripts, cmake/InnPaths.cmake).

inn_program(gencancel SOURCES gencancel.cc LIBS ${INN_CANLOCK_LIBS})

inn_scripts(controlbatch controlchan docheckgroups perl-nocem pgpverify)

# control/Makefile's install target: gencancel (LI_XPRI), the scripts
# (CP_XPUB) and controlchan's modules (CP_RPUB).
inn_install(TARGETS gencancel DESTINATION ${bindir} MODE XPRI)
inn_install(FILES controlbatch controlchan docheckgroups perl-nocem pgpverify
            DESTINATION ${bindir} MODE XPUB)
inn_install(FILES modules/checkgroups.pl modules/ihave.pl modules/newgroup.pl
                  modules/rmgroup.pl modules/sendme.pl
            DESTINATION ${CONTROLDIR} MODE RPUB)
