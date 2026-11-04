# Install-time steps for cmake/InnInstall.cmake.  cmake --install includes
# this file through an install(CODE) rule in the root CMakeLists.txt, which
# first sets the INN_I_* variables; inn_install_begin runs before any file
# is installed and inn_install_end after the last.  The install scripts of
# all directories run in one scope, so what these set stays visible.

# inn_install_begin: refuse test builds, note whether we run as root, and
# create INN's directories (the top Makefile's "directories" target).
macro(inn_install_begin)
    if(INN_I_TEST_BUILD)
        message(FATAL_ERROR
            "This build sets INN_RUNAS_USER or INN_RUNAS_GROUP, so its "
            "programs accept ${INN_I_USER} as the news user.  Test builds "
            "must never be installed; configure a build without them.")
    endif()
    execute_process(COMMAND id -u OUTPUT_VARIABLE INN_I_UID
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND id -un OUTPUT_VARIABLE INN_I_ME
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(INN_I_ROOT FALSE)
    if(INN_I_UID STREQUAL "0")
        set(INN_I_ROOT TRUE)
    endif()
    # Directories that already exist keep their mode and owner, as with
    # install-sh -d.
    set(INN_I_NEW_DIRS)
    foreach(dir ${INN_I_DIRS} ${INN_I_RUNDIR})
        set(d "$ENV{DESTDIR}${dir}")
        if(NOT IS_DIRECTORY "${d}")
            file(MAKE_DIRECTORY "${d}")
            if(dir STREQUAL INN_I_RUNDIR)
                file(CHMOD "${d}" DIRECTORY_PERMISSIONS
                     OWNER_READ OWNER_WRITE OWNER_EXECUTE
                     GROUP_READ GROUP_EXECUTE)
            else()
                file(CHMOD "${d}" DIRECTORY_PERMISSIONS
                     OWNER_READ OWNER_WRITE OWNER_EXECUTE
                     GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
            endif()
            list(APPEND INN_I_NEW_DIRS "${d}")
        endif()
    endforeach()
endmacro()

# inn_install_if_absent(<src> <dest> <permissions>): install a
# configuration file unless one is already there.
function(inn_install_if_absent src dest perms)
    if(EXISTS "$ENV{DESTDIR}${dest}")
        message(STATUS "Keeping: $ENV{DESTDIR}${dest}")
        return()
    endif()
    get_filename_component(dir "${dest}" DIRECTORY)
    get_filename_component(name "${dest}" NAME)
    file(INSTALL DESTINATION "${dir}" TYPE FILE FILES "${src}"
         RENAME "${name}" PERMISSIONS ${perms})
endfunction()

# _inn_chown(<owner:group> <path>...)
function(_inn_chown who)
    if(ARGN)
        execute_process(COMMAND chown ${who} ${ARGN}
                        RESULT_VARIABLE rc)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "chown ${who} failed")
        endif()
    endif()
endfunction()

# _inn_new_data(<dest> [<src>]): first-install data that must never be
# overwritten (site/Makefile's SPECIAL): copied from <src>, or empty.
function(_inn_new_data dest)
    set(d "$ENV{DESTDIR}${dest}")
    if(EXISTS "${d}")
        return()
    endif()
    if(ARGC GREATER 1)
        file(COPY_FILE "${ARGV1}" "${d}")
    else()
        file(TOUCH "${d}")
    endif()
    file(CHMOD "${d}" PERMISSIONS ${INN_I_DATA_MODE})
    if(INN_I_ROOT)
        _inn_chown(${INN_I_USER}:${INN_I_GROUP} "${d}")
    endif()
    message(STATUS "Created: ${d}")
endfunction()

# inn_install_end: ownership, innbind and inews, then the first-install
# data (active, active.times, newsgroups, history).
macro(inn_install_end)
    set(innbind "$ENV{DESTDIR}${INN_I_BINDIR}/innbind")
    set(inews "$ENV{DESTDIR}${INN_I_BINDIR}/inews")
    if(INN_I_ROOT)
        # Everything installed is owned by news (make install's OWNER),
        # then innbind by root (LI_SPRI).  chown clears the setuid and
        # setgid bits, so the special modes are set after it.
        _inn_chown(${INN_I_USER}:${INN_I_GROUP}
                   ${CMAKE_INSTALL_MANIFEST_FILES} ${INN_I_NEW_DIRS})
        _inn_chown(root:${INN_I_GROUP} "${innbind}")
        execute_process(COMMAND chmod 4550 "${innbind}")
        if(INN_I_SETGID_INEWS)
            execute_process(COMMAND chmod 2555 "${inews}")
        endif()
    else()
        message(STATUS "")
        message(STATUS "Not run as root: the installed files are owned by "
                       "${INN_I_ME}, not ${INN_I_USER}:${INN_I_GROUP}.")
        message(STATUS "${innbind} needs to be installed setuid root "
                       "(owner root, group ${INN_I_GROUP}, mode 4550).")
        if(INN_I_SETGID_INEWS)
            message(STATUS "${inews} needs mode 2555 (setgid "
                           "${INN_I_GROUP}).")
        endif()
        message(STATUS "")
    endif()

    _inn_new_data("${INN_I_DBDIR}/active" "${INN_I_ACTIVE_MINIMAL}")
    _inn_new_data("${INN_I_DBDIR}/newsgroups" "${INN_I_NEWSGROUPS_MINIMAL}")
    _inn_new_data("${INN_I_DBDIR}/active.times")

    # History, as site/Makefile does it.  Staged (DESTDIR) installs get an
    # empty history file at the default place and leave the rest to the
    # package.  A real install asks the installed inn.conf where history is
    # and which method it uses, and builds an empty database, but only as
    # root or the news user (the programs drop from root to news).
    if(DEFINED ENV{DESTDIR} AND NOT "$ENV{DESTDIR}" STREQUAL "")
        _inn_new_data("${INN_I_HISTORYDIR}/history")
    else()
        set(can_run FALSE)
        if(INN_I_ROOT OR INN_I_ME STREQUAL INN_I_USER)
            set(can_run TRUE)
        endif()
        execute_process(COMMAND ${INN_I_BINDIR}/innconfval pathhistory
                        OUTPUT_VARIABLE histdir RESULT_VARIABLE rc
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT rc EQUAL 0 OR histdir STREQUAL "")
            message(FATAL_ERROR
                "innconfval pathhistory failed; not initializing history")
        endif()
        execute_process(COMMAND ${INN_I_BINDIR}/innconfval hismethod
                        OUTPUT_VARIABLE hismethod
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(hismethod STREQUAL "hissqlite")
            if(NOT EXISTS "${histdir}/history.sqlite" AND can_run)
                execute_process(COMMAND ${INN_I_BINDIR}/makehistory
                                RESULT_VARIABLE rc)
                if(NOT rc EQUAL 0)
                    message(FATAL_ERROR "makehistory failed")
                endif()
            endif()
        elseif(NOT EXISTS "${histdir}/history")
            _inn_new_data("${histdir}/history")
            if(can_run)
                execute_process(COMMAND ${INN_I_BINDIR}/makedbz -i -o
                                RESULT_VARIABLE rc)
                if(NOT rc EQUAL 0)
                    message(FATAL_ERROR "makedbz -i -o failed")
                endif()
            endif()
        endif()
    endif()

    message(STATUS "")
    message(STATUS "If this is a first-time installation, a minimal active "
                   "file and history database have been installed.  Do not "
                   "forget to update your cron entries and configure INN.")
endmacro()
