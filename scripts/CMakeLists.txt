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
