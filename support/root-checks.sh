#!/bin/bash
#
#  Checks of what INN does when started as root, which the normal checks
#  (run as the tester) cannot reach.  Run by hand with sudo after the normal
#  checks pass, and record the output in the commit:
#
#      sudo support/root-checks.sh [build-dir]     (default: build-live)
#
#  Use a build configured with INN_RUNAS_USER/INN_RUNAS_GROUP set to the
#  tester's account (build-live), so "dropping to the news user" means
#  becoming the tester.  Everything is compiled and run in a temporary
#  directory, so no root-owned files are left in the tree or the build.
#  Prints one line per check and a RESULT line, like support/verify-cxx.sh.

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/${1:-build-live}
fail=0

res() {
    printf '  %-50s %s\n' "$1" "$2"
    [[ "$2" == PASS* ]] || fail=1
}

if [ "$(id -u)" != 0 ]; then
    echo "must be run as root: sudo $0 $*" >&2
    exit 2
fi
[ -f "$build/CMakeCache.txt" ] || { echo "no build in $build" >&2; exit 2; }
runas=$(sed -n 's/^INN_RUNAS_USER:STRING=//p' "$build/CMakeCache.txt")
if [ -z "$runas" ] || [ "$runas" = root ]; then
    echo "$build is not configured with INN_RUNAS_USER (use build-live)" >&2
    exit 2
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/root-checks.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# --- lib/newsuser: the shared privilege drop ----------------------------------

# Compile the C test as C, then link with the C++ driver, which adds the
# platform's C++ runtime for the converted libinn (libc++ on macOS,
# libstdc++ on Linux).
cflags=(-I"$build/include" -I"$root/include" -I"$root/tests" -DTESTING)
if cc "${cflags[@]}" -c -o "$tmp/newsuser-t.o" "$root/tests/lib/newsuser-t.c" \
      >"$tmp/cc.log" 2>&1 \
   && cc "${cflags[@]}" -c -o "$tmp/basic.o" "$root/tests/tap/basic.c" \
      >>"$tmp/cc.log" 2>&1 \
   && c++ -o "$tmp/newsuser.t" "$tmp/newsuser-t.o" "$tmp/basic.o" \
      "$build/lib/libinn.a" >>"$tmp/cc.log" 2>&1; then
    out=$("$tmp/newsuser.t" 2>&1)
    if echo "$out" | grep -q '^not ok' || ! echo "$out" | grep -q '^ok'; then
        res "lib/newsuser test" FAIL
        echo "$out" | sed 's/^/    /'
    else
        res "lib/newsuser test" "PASS ($(echo "$out" | grep -c '^ok') checks)"
        echo "$out" | sed 's/^/    /'
    fi
else
    res "lib/newsuser test" "FAIL (build)"
    sed 's/^/    /' "$tmp/cc.log" | tail -10
fi

if [ $fail = 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
    exit 1
fi
