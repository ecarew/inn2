#!/usr/bin/env bash
#
#  Conversion-era compatibility checks, put on ice when INN++ left the
#  conversion stage (the architect, 2026-11-17).  Nothing runs them
#  automatically; run them by hand to debug, or to answer a question about
#  compatibility with the original C build.  They may have gone stale since:
#  fix one when it is next needed.
#
#  Usage: support/compat-checks.sh [build-dir]     (default: build)
#
#  The libc-replacement unit tests are iced as well: ctest -L iced.

root=$(cd "$(dirname "$0")/.." && pwd)
data=$root/support/verify-cxx
build=$root/${1:-build}
tmp=$(mktemp -d /tmp/compat-checks.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
fail=0

res() {
    printf '  %-42s %s\n' "$1" "$2"
    [[ "$2" == PASS* || "$2" == SKIP* ]] || fail=1
}

cd "$root" || exit 1

# Every C++ file compiles as C++20; per-target settings once left most of
# the tree on the compiler default without notice.
not20=$(python3 - "$build/compile_commands.json" <<'PY' 2>/dev/null
import json, sys
db = json.load(open(sys.argv[1]))
print(sum(1 for e in db if e["file"].endswith(".cc")
          and "-std=c++20" not in e["command"].split()))
PY
)
if [ "$not20" = 0 ]; then
    res "every .cc compiles as -std=c++20" PASS
else
    res "every .cc compiles as -std=c++20" "FAIL (${not20:-?} files)"
fi

# defined_symbols FILE: "W name" or "T name" for each defined external symbol
# (W for weak), without the leading underscore Mach-O adds to C names.  Apple's
# nm takes -gUm; GNU nm takes -g --defined-only and marks weak symbols W or V.
defined_symbols() {
    if nm -gUm "$1" >/dev/null 2>&1; then
        nm -gUm "$1" | awk '/ external / {
            name = $NF; sub(/^_/, "", name)
            print (/ weak / ? "W" : "T"), name }'
    else
        nm -g --defined-only "$1" | awk 'NF >= 3 {
            print ($2 ~ /^[WwVv]$/ ? "W" : "T"), $3 }'
    fi
}

# Every symbol the Makefile-built innd exported (innd-symbols.txt, Mach-O
# names) is still exported, under its C name, except those the refactoring
# retired on purpose (innd-retired.txt), which must be gone.
defined_symbols "$build/innd/innd" | awk '{print $2}' | sort >"$tmp/syms"
awk '!/^#/ && NF { print $1 }' "$data/innd-retired.txt" | sort >"$tmp/retired"
# __mh_execute_header is the Mach-O header symbol Apple's linker adds.
sed 's/^_//' "$data/innd-symbols.txt" | grep -vx '_mh_execute_header' \
    | sort | comm -23 - "$tmp/retired" >"$tmp/want-syms"
want=$(wc -l <"$tmp/want-syms" | tr -d ' ')
missing=$(comm -23 "$tmp/want-syms" "$tmp/syms" | wc -l | tr -d ' ')
if [ "$missing" = 0 ]; then
    res "innd exports all $want Make-build symbols" PASS
else
    res "innd exports all $want Make-build symbols" "FAIL ($missing missing)"
fi
back=$(comm -12 "$tmp/retired" "$tmp/syms" | wc -l | tr -d ' ')
if [ "$back" = 0 ]; then
    res "retired innd symbols are gone ($(wc -l <"$tmp/retired" | tr -d ' '))" PASS
else
    res "retired innd symbols are gone" "FAIL ($back still exported)"
fi

# gencancel against the original C gencancel from main, built with the
# original Autoconf/Make libraries when they are present.
gcdir=$tmp/gencancel
mkdir -p "$gcdir/etc"
printf 'cancels {\n    canlockuser:  [ "user-secret" ]\n    canlockadmin: [ "admin-secret" ]\n}\n' \
    >"$gcdir/etc/inn-secrets.conf"
gencancel_runs() (
    local g=$1
    export INNCONF=$root/tests/data/etc/inn.conf
    cd "$gcdir" || exit 1
    "$g" -n test.group -f admin@live.test -s 'cancel test' -b 'Body.' \
        '<orig@live.test>'
    echo "--"; "$g" -a -n test.group -f admin@live.test '<orig@live.test>'
    echo "--"; "$g" -m -c UTF-8 -n test.group -f admin@live.test '<o@l.t>'
    echo "--"; "$g" -d -n a.b,c.d -f admin@live.test '<o@l.t>'
    echo "--"; "$g" -k -n x -f admin@live.test '<a@b>' 2>&1 | grep -v inn-secrets
    echo "exit $?"
    echo "--"; "$g" -L -n x -f admin@live.test '<a@b>' 2>&1 | grep -v inn-secrets
    echo "exit $?"
)
mask='s/^(Date|Message-ID): .*/\1: (masked)/'
if [ -f lib/.libs/libinn.a ] && [ -f storage/.libs/libinnstorage.a ] \
   && git show main:control/gencancel.c >"$tmp/gencancel-orig.c" 2>/dev/null \
   && cc -g -I"$root/include" -c "$tmp/gencancel-orig.c" -o "$tmp/gco.o" \
        2>"$tmp/gco.log" \
   && cc "$tmp/gco.o" lib/.libs/libinn.a history/.libs/libinnhist.a \
        storage/.libs/libinnstorage.a lib/.libs/libinn.a lib/canlock.o \
        -lz -lsqlite3 -o "$tmp/gencancel-orig" 2>>"$tmp/gco.log"; then
    gencancel_runs "$build/control/gencancel" 2>/dev/null | sed -E "$mask" \
        >"$tmp/gencancel.out"
    gencancel_runs "$tmp/gencancel-orig" 2>/dev/null | sed -E "$mask" \
        >"$tmp/gencancel-orig.out"
    if cmp -s "$tmp/gencancel.out" "$tmp/gencancel-orig.out"; then
        res "gencancel matches original C" PASS
    else
        res "gencancel matches original C" FAIL
    fi
else
    res "gencancel matches original C" "SKIP (no original Make build)"
fi

if [ $fail = 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
    exit 1
fi
