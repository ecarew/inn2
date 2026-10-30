# Sample configuration files (CMake-only goal item 3), written into the build
# tree.  So far only inn.conf, which the tests and harnesses need
# (lib/innconf, support/live-innd.sh, support/root-checks.sh); the other
# samples still come from fixconfig in the Make build.
#
# The values are the ones support/fixconfig substitutes, with configure's
# defaults: the INN directories under INN_PREFIX (include/CMakeLists.txt,
# m4/paths.m4), the host name, sendmail and whether a PGP verifier exists.

set(prefix ${INN_PREFIX})
set(bindir ${prefix}/bin)
set(sysconfdir ${INN_PATH_CONFIG})
set(tmpdir ${INN_PATH_TMP})
set(CONTROLDIR ${bindir}/control)
set(DBDIR ${prefix}/db)
set(FILTERDIR ${bindir}/filter)
set(HISTORYDIR ${prefix}/db)
set(HTTPDIR ${prefix}/http)
set(LOGDIR ${prefix}/log)
set(RUNDIR ${prefix}/run)
set(SPOOLDIR ${prefix}/spool)

# configure uses `hostname`, which is gethostname().
cmake_host_system_information(RESULT HOSTNAME QUERY HOSTNAME)

# configure looks in /usr/sbin and /usr/lib and stops if sendmail is not
# there; inn.conf only records the path (mta), so a missing one is a warning.
find_program(INN_SENDMAIL sendmail PATHS /usr/sbin /usr/lib NO_DEFAULT_PATH)
if(NOT INN_SENDMAIL)
    message(WARNING "sendmail not found; inn.conf's mta names "
                    "/usr/sbin/sendmail.  Set INN_SENDMAIL to change it.")
    set(INN_SENDMAIL /usr/sbin/sendmail CACHE FILEPATH "sendmail" FORCE)
endif()
set(SENDMAIL ${INN_SENDMAIL})

# pgpverify is on if any PGP or GnuPG verifier is installed (configure.ac).
find_program(INN_PGP_VERIFIER NAMES gpgv1 gpgv gpgv2 gpg1 gpg gpg2 pgpv pgp
             pgpgpg)
if(INN_PGP_VERIFIER)
    set(DO_PGPVERIFY true)
else()
    set(DO_PGPVERIFY false)
endif()

configure_file(inn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/inn.conf @ONLY)
