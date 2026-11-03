# contrib/ programs — converted from contrib/Makefile.  The scripts there
# (.in files) are not generated yet (CLAUDE.md, CMake item 7).

# crypt() is in libc on macOS and the BSDs but in libcrypt on glibc systems
# (same check as authprogs/CMakeLists.txt; the results are cached).
include(CheckFunctionExists)
include(CheckLibraryExists)
check_function_exists(crypt INN_CRYPT_IN_LIBC)
set(_crypt)
if(NOT INN_CRYPT_IN_LIBC)
    check_library_exists(crypt crypt "" INN_HAVE_LIBCRYPT)
    if(INN_HAVE_LIBCRYPT)
        set(_crypt crypt)
    endif()
endif()

inn_program(auth_pass  SOURCES auth_pass.cc  INNONLY LIBS ${_crypt})
inn_program(expirectl  SOURCES expirectl.cc  NOLIBS)
inn_program(mlockfile  SOURCES mlockfile.cc  NOLIBS)
inn_program(newsresp   SOURCES newsresp.cc   NOLIBS)
inn_program(pullart    SOURCES pullart.cc    INNONLY)
inn_program(reset-cnfs SOURCES reset-cnfs.cc NOLIBS)
inn_program(respool    SOURCES respool.cc)

# fixscript -i (no INN library line) in the Makefile:
inn_scripts(analyze-traffic archivegz authmysql count_overview fixhist
            innconfcheck mkbuf nnrp.access2readers.conf stathist tunefeed
            NOLIB)
inn_scripts(backlogstat cleannewsgroups findreadgroups makeexpctl
            makestorconf ovsqlite-dump ovsqlite-undump thdexpire)
