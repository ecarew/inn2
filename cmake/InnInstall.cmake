# Install rules (CMake-only goal item 5): what make install did, with its
# modes and ownership.  Sources: Makefile.global.in's LI_* and CP_* macros,
# each directory's install target, the top Makefile's directories, and
# site/Makefile for the configuration files and the first-install data.
#
#   cmake --install build          (as root, for a real server)
#   DESTDIR=/tmp/stage cmake --install build   (staging, for a package)
#
# Files go to the absolute directories of cmake/InnPaths.cmake, which are
# compiled into INN, so cmake --install's --prefix has no effect; set
# INN_PREFIX when configuring instead.  DESTDIR is honoured.
#
# Ownership (cmake/InnInstallSteps.cmake, run at the end of the install):
# as root, everything installed is chowned to the news user and group, and
# innbind becomes setuid root (4550, group news), the only setuid-root
# file.  As anyone else, files keep the installer as owner and innbind
# stays 0550, with a note, as make install did.
#
# Differences from make install:
# - Configuration files in etc/, filter/ and http/ are installed only if
#   absent, never replaced (make install replaced them when the copy in
#   site/ was newer, keeping the old one as .OLD).
# - Programs are not backed up as .OLD.
# - Man pages and the documentation (goal item 6), and the Perl and Python
#   filter programs (item 8), are not installed yet.
# - A build with INN_RUNAS_USER or INN_RUNAS_GROUP (a test build, whose
#   programs accept the tester as the news user) refuses to install.

set(INN_INSTALL_STEPS ${CMAKE_CURRENT_LIST_DIR}/InnInstallSteps.cmake)

# Directories not in cmake/InnPaths.cmake (Makefile.global.in, m4/paths.m4).
set(docdir ${prefix}/doc)
set(includedir ${prefix}/include)
set(datarootdir ${prefix}/share)
set(PATHAUTH ${bindir}/auth)
set(PATHAUTHPASSWD ${PATHAUTH}/passwd)
set(PATHAUTHRESOLV ${PATHAUTH}/resolv)
set(PATHRNEWS ${bindir}/rnews.libexec)

# File modes, named as Makefile.global.in names them: X executable, R
# readable, W writable by the owner; PRI for the news group only, PUB for
# everyone.  DATA is the mode of article files (INN_ARTFILE_MODE), for
# active and newsgroups.
set(INN_MODE_XPRI OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
set(INN_MODE_XPUB ${INN_MODE_XPRI} WORLD_READ WORLD_EXECUTE)
set(INN_MODE_RPRI OWNER_READ OWNER_WRITE GROUP_READ)
set(INN_MODE_RPUB ${INN_MODE_RPRI} WORLD_READ)
set(INN_MODE_WXPUB OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
    GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# _inn_mode_list(<out> <octal>): an octal mode such as 0664 as CMake's
# permission keywords (the special bits are not needed here).
function(_inn_mode_list out octal)
    string(REGEX REPLACE "^0*" "" octal "${octal}")
    string(LENGTH "${octal}" n)
    while(n LESS 3)
        string(PREPEND octal 0)
        string(LENGTH "${octal}" n)
    endwhile()
    set(result)
    set(i 0)
    foreach(who OWNER GROUP WORLD)
        string(SUBSTRING "${octal}" ${i} 1 d)
        math(EXPR i "${i} + 1")
        if(d GREATER_EQUAL 4)
            list(APPEND result ${who}_READ)
            math(EXPR d "${d} - 4")
        endif()
        if(d GREATER_EQUAL 2)
            list(APPEND result ${who}_WRITE)
            math(EXPR d "${d} - 2")
        endif()
        if(d GREATER_EQUAL 1)
            list(APPEND result ${who}_EXECUTE)
        endif()
    endforeach()
    set(${out} ${result} PARENT_SCOPE)
endfunction()
_inn_mode_list(INN_MODE_DATA ${INN_ARTFILE_MODE})
# inews (INEWSMODE: 0550, or 02555 with INN_SETGID_INEWS, whose setgid bit
# is set at the end of the install, after chown) and rnews (RNEWSMODE).
if(INN_SETGID_INEWS)
    set(INN_MODE_INEWS ${INN_MODE_XPUB})
else()
    set(INN_MODE_INEWS ${INN_MODE_XPRI})
endif()
set(INN_MODE_RNEWS OWNER_READ OWNER_EXECUTE)

# inn_install(TARGETS|FILES <item>... DESTINATION <dir> MODE <mode>
#             [RENAME <name>])
# Install programs (TARGETS) or files from the build or source directory
# (FILES, relative to the current build directory if generated there,
# else the current source directory) with one of the modes above.
function(inn_install kind)
    cmake_parse_arguments(I "" "DESTINATION;MODE;RENAME" "" ${ARGN})
    set(perms ${INN_MODE_${I_MODE}})
    if(NOT perms)
        message(FATAL_ERROR "inn_install: unknown mode ${I_MODE}")
    endif()
    set(rename)
    if(I_RENAME)
        set(rename RENAME ${I_RENAME})
    endif()
    if(kind STREQUAL "TARGETS")
        install(TARGETS ${I_UNPARSED_ARGUMENTS}
                RUNTIME DESTINATION ${I_DESTINATION} PERMISSIONS ${perms})
    elseif(kind STREQUAL "FILES")
        set(files)
        foreach(f ${I_UNPARSED_ARGUMENTS})
            if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${f})
                list(APPEND files ${CMAKE_CURRENT_BINARY_DIR}/${f})
            else()
                list(APPEND files ${CMAKE_CURRENT_SOURCE_DIR}/${f})
            endif()
        endforeach()
        install(FILES ${files} DESTINATION ${I_DESTINATION}
                PERMISSIONS ${perms} ${rename})
    else()
        message(FATAL_ERROR "inn_install: TARGETS or FILES, not ${kind}")
    endif()
endfunction()

# inn_install_config(<file>... DESTINATION <dir> MODE <mode>)
# Install configuration files only if they are not there yet.  A file is
# taken from the current build directory if generated there (inn.conf,
# newsfeeds, ...), else from the current source directory.
function(inn_install_config)
    cmake_parse_arguments(I "" "DESTINATION;MODE" "" ${ARGN})
    if(NOT INN_MODE_${I_MODE})
        message(FATAL_ERROR "inn_install_config: unknown mode ${I_MODE}")
    endif()
    foreach(f ${I_UNPARSED_ARGUMENTS})
        if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${f})
            set(src ${CMAKE_CURRENT_BINARY_DIR}/${f})
        else()
            set(src ${CMAKE_CURRENT_SOURCE_DIR}/${f})
        endif()
        get_filename_component(name ${f} NAME)
        install(CODE "inn_install_if_absent(\"${src}\" \"${I_DESTINATION}/${name}\" \"${INN_MODE_${I_MODE}}\")")
    endforeach()
endfunction()
