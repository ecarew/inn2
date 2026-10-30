#!/bin/bash
#
#  Compare the config.h CMake writes with the one ./configure wrote, symbol
#  by symbol (CMake-only goal item 1).
#
#      support/compare-config.sh [cmake-config.h [configure-config.h]]
#
#  Defaults: build/config-cmake.h and include/config.h.  Prints one line per
#  symbol whose value differs ("undef" for a symbol that is not defined), and
#  one per configure symbol that CMake does not write and that is not in the
#  list of symbols left out on purpose below.  Exits 0 if there is nothing to
#  report.

root=$(cd "$(dirname "$0")/.." && pwd)
cmake_h=${1:-$root/build/config-cmake.h}
conf_h=${2:-$root/include/config.h}

# configure symbols the CMake build leaves out: none of INN's sources uses
# them.  Autoconf and libtool bookkeeping, checks that select lib/
# replacement functions (CMake chooses those itself), large-file and
# Solaris type workarounds, and the type fallbacks for systems without
# <stdint.h> and <sys/types.h>.
dropped="AC_APPLE_UNIVERSAL_BUILD LT_OBJDIR STDC_HEADERS YYTEXT_POINTER
    PACKAGE_BUGREPORT PACKAGE_NAME PACKAGE_STRING PACKAGE_TARNAME PACKAGE_URL
    PACKAGE_VERSION SIZEOF_LONG HAVE_DLFCN_H HAVE_MINIX_CONFIG_H HAVE_STDIO_H
    HAVE_STDLIB_H HAVE_STRING_H HAVE_SYS_STAT_H HAVE_SYS_TYPES_H HAVE_WCHAR_H
    HAVE_KRB5 HAVE_KRB5_GET_ERR_TXT HAVE_KRB5_KRB5_H HAVE_PAM_PAM_APPL_H
    HAVE_PREAD HAVE_PWRITE HAVE_REALLOCARRAY HAVE_STRLCAT HAVE_STRLCPY
    HAVE_STRNCASECMP HAVE_STRUCT_SOCKADDR_IN6 HAVE_TM_ZONE HAVE_DECL_TZNAME
    HAVE_UINTPTR_T HAVE_UNSIGNED_LONG_LONG_INT TM_IN_SYS_TIME _GNU_SOURCE
    _FILE_OFFSET_BITS _LARGEFILE_SOURCE _LARGE_FILES _TIME_BITS _UINT32_T
    _UINT64_T _UINT8_T __MINGW_USE_VC2005_COMPAT const gid_t int8_t int16_t
    int32_t int64_t off_t pid_t size_t ssize_t uid_t uint8_t uint16_t uint32_t
    uint64_t uintptr_t"

# Symbols where CMake's answer is right and configure's is not, with why.
# HAVE_STRUCT_TM_TM_GMTOFF: configure tests struct tm with Autoconf's default
# includes, which lack <time.h>, so the test fails ("incomplete type 'struct
# tm'" in config.log) even where the member exists, as on macOS and glibc.
known="HAVE_STRUCT_TM_TM_GMTOFF"

# The list above on one line, for matching with case.
dropped_words=$(echo "$dropped" | tr -s ' \n' '  ')

# symbols FILE: "NAME value" for each #define and "NAME undef" for each
# commented-out #undef, outside the system-extensions block.
symbols() {
    awk '
        /^#ifndef /                 { skip = 1; next }
        skip && /^#endif/           { skip = 0; next }
        skip                        { next }
        /^#define [A-Za-z_][A-Za-z0-9_]*/ {
            v = $3; for (i = 4; i <= NF; i++) v = v " " $i
            print $2, (v == "" ? "(empty)" : v); next }
        /^\/\* #undef [A-Za-z_][A-Za-z0-9_]* \*\// { print $3, "undef" }
    ' "$1" | sort
}

symbols "$cmake_h" >"${TMPDIR:-/tmp}/cmake-syms.$$"
symbols "$conf_h" >"${TMPDIR:-/tmp}/conf-syms.$$"
status=0
while read -r name conf_value; do
    cmake_value=$(awk -v n="$name" \
                      '$1 == n { $1 = ""; sub(/^ /, ""); print }' \
                      "${TMPDIR:-/tmp}/cmake-syms.$$")
    if [ -z "$cmake_value" ]; then
        case " $dropped_words " in
        *" $name "*) ;;
        *) echo "missing in CMake: $name (configure: $conf_value)"; status=1 ;;
        esac
    elif [ "$cmake_value" != "$conf_value" ]; then
        case " $known " in
        *" $name "*)
            echo "known difference: $name: CMake $cmake_value," \
                 "configure $conf_value" ;;
        *)  echo "differs: $name: CMake $cmake_value, configure $conf_value"
            status=1 ;;
        esac
    fi
done <"${TMPDIR:-/tmp}/conf-syms.$$"
# Symbols CMake defines that configure's config.h does not have at all.  (An
# undefined one is harmless: an older configure may not know the name.)
awk '$2 != "undef" { print $1 }' "${TMPDIR:-/tmp}/cmake-syms.$$" \
    | while read -r name; do
        grep -q "^$name " "${TMPDIR:-/tmp}/conf-syms.$$" \
            || { echo "only in CMake: $name"; }
    done | grep . && status=1
rm -f "${TMPDIR:-/tmp}/cmake-syms.$$" "${TMPDIR:-/tmp}/conf-syms.$$"
[ $status = 0 ] && echo "config.h: CMake and configure agree"
exit $status
