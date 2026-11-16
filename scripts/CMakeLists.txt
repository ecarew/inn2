# scripts/ — converted from scripts/Makefile.  innshellvars, innshellvars.pl
# and innshellvars.tcl were written by configure (AC_CONFIG_FILES); the
# programs went through support/fixscript (inn_scripts).  Both now come from
# cmake/InnPaths.cmake, into the build tree.

foreach(vars innshellvars innshellvars.pl innshellvars.tcl)
    configure_file(${vars}.in ${CMAKE_CURRENT_BINARY_DIR}/${vars} @ONLY)
endforeach()

inn_scripts(inncheck innmail innreport innstat innwatch news.daily rc.news
            scanlogs tally.control writelog)
inn_scripts(innupgrade simpleftp NOLIB)

# scripts/Makefile's install target.
inn_install(FILES innmail innreport simpleftp DESTINATION ${bindir} MODE XPUB)
inn_install(FILES inncheck innstat innupgrade innwatch news.daily rc.news
                  scanlogs tally.control writelog
            DESTINATION ${bindir} MODE XPRI)
inn_install(FILES innreport-display.conf innreport_inn.pm innshellvars
                  innshellvars.pl innshellvars.tcl
            DESTINATION ${libdir} MODE RPUB)

# inn.service, the systemd unit (scripts/inn.service.in), with this build's
# directories and account.  Type=notify when innd is built with libsystemd
# (it then reports READY=1), else forking.  ProtectHome=true would hide an
# INN under /home or /root, so it is off then.  RuntimeDirectory= recreates
# a run directory under /run, which is emptied at boot.  Installed only
# with a unit directory (INN_SYSTEMD_UNIT_DIR, or systemd's own).
if(HAVE_SD_NOTIFY)
    set(INN_UNIT_TYPE notify)
else()
    set(INN_UNIT_TYPE forking)
endif()
set(INN_UNIT_PROTECT_HOME true)
foreach(dir ${prefix} ${DBDIR} ${HISTORYDIR} ${SPOOLDIR} ${LOGDIR} ${RUNDIR}
            ${tmpdir} ${HTTPDIR} ${sysconfdir})
    if(dir MATCHES "^/(home|root|run/user)(/|$)")
        set(INN_UNIT_PROTECT_HOME false)
    endif()
endforeach()
set(INN_UNIT_RUNTIME_DIRECTORY "")
if(RUNDIR MATCHES "^/run/(.+)$")
    set(INN_UNIT_RUNTIME_DIRECTORY "RuntimeDirectory=${CMAKE_MATCH_1}\nRuntimeDirectoryPreserve=yes")
endif()
configure_file(inn.service.in ${CMAKE_CURRENT_BINARY_DIR}/inn.service @ONLY)
if(INN_UNIT_DIR)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/inn.service
            DESTINATION ${INN_UNIT_DIR} PERMISSIONS ${INN_MODE_RPUB})
endif()
