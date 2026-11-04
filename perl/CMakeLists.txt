# perl/ — converted from perl/Makefile.  INN::Config was written by
# configure (AC_CONFIG_FILES); INN::Utils::Shlock went through
# support/fixscript.  Both come from cmake/InnPaths.cmake, into the build
# tree.

configure_file(INN/Config.pm.in ${CMAKE_CURRENT_BINARY_DIR}/INN/Config.pm
               @ONLY)
inn_scripts(INN/Utils/Shlock.pm)

# perl/Makefile's install target (CP_RPUB).
inn_install(FILES INN/Config.pm INN/ovsqlite_client.pm
            DESTINATION ${LIBPERLDIR}/INN MODE RPUB)
inn_install(FILES INN/Utils/Shlock.pm DESTINATION ${LIBPERLDIR}/INN/Utils
            MODE RPUB)
