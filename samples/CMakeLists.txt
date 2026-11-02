# Sample configuration files (CMake-only goal item 3), written into the build
# tree: the ones support/fixconfig and support/fixscript make in the Make
# build (samples/Makefile).  The other samples need no changes and are used
# as they are.
#
# The directories, Perl and inn_fixscript come from cmake/InnPaths.cmake.
# fixconfig also substitutes the host name, sendmail and whether a PGP
# verifier exists, with configure's defaults.

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

foreach(sample buffindexed.conf inn.conf innreport.conf newsfeeds)
    configure_file(${sample}.in ${CMAKE_CURRENT_BINARY_DIR}/${sample} @ONLY)
endforeach()
foreach(script nnrpd_auth.pl nnrpd_access.pl)
    inn_fixscript(${CMAKE_CURRENT_SOURCE_DIR}/${script}.in
                  ${CMAKE_CURRENT_BINARY_DIR}/${script})
endforeach()
