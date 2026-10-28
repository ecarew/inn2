#!/bin/bash
#
#  Verification checks for the INN++ C++ conversion.  Run from anywhere after
#  "cmake -S . -B build" has been configured; prints one line per check and a
#  final RESULT line, and exits non-zero if anything failed.  Paste the output
#  into the commit message's "Tested:" section (see CLAUDE.md).
#
#  Usage: support/verify-cxx.sh [build-dir]      (default: build)

root=$(cd "$(dirname "$0")/.." && pwd)
data=$root/support/verify-cxx
build=$root/${1:-build}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/verify-cxx.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
fail=0

res() {
    printf '  %-42s %s\n' "$1" "$2"
    [[ "$2" == PASS* ]] || fail=1
}

cd "$root" || exit 1

if cmake --build "$build" -j8 >"$tmp/build.log" 2>&1; then
    res "cmake build (lib, history, storage, innd)" PASS
else
    res "cmake build (lib, history, storage, innd)" FAIL
    tail -20 "$tmp/build.log" >&2
    echo "RESULT: FAIL"
    exit 1
fi

# Every symbol the Makefile-built innd exports (innd-symbols.txt) must still
# be exported, under its C name, by the CMake build.
nm -gU "$build/innd/innd" | awk '{print $3}' | sort >"$tmp/syms"
want=$(wc -l <"$data/innd-symbols.txt" | tr -d ' ')
missing=$(comm -23 "$data/innd-symbols.txt" "$tmp/syms" | wc -l | tr -d ' ')
if [ "$missing" = 0 ]; then
    res "innd exports all $want Make-build symbols" PASS
else
    res "innd exports all $want Make-build symbols" "FAIL ($missing missing)"
fi

mangled=0
for a in "$build/innd/innd" "$build"/history/libinnhist.a \
         "$build"/storage/libinnstorage.a "$build"/lib/libinn.a; do
    n=$(nm -gU "$a" 2>/dev/null | grep -c ' __Z')
    mangled=$((mangled + n))
done
if [ "$mangled" = 0 ]; then
    res "C linkage kept (no mangled exports)" PASS
else
    res "C linkage kept (no mangled exports)" "FAIL ($mangled mangled)"
fi

if "$build/innd/innd" -h 2>&1 | grep -q "can't resolve news to a GID"; then
    res "innd starts (reaches news-group check)" PASS
else
    res "innd starts (reaches news-group check)" FAIL
fi

# tinyleaf names each article file by the MD5 of its message-ID.
mkdir "$tmp/spool"
"$build/innd/tinyleaf" "$tmp/spool" <"$data/tinyleaf.in" >"$tmp/tl.out" 2>&1
noversion='s/^100 tinyfeed from .*/100 tinyfeed from INN/'
if diff -q <(sed "$noversion" "$tmp/tl.out") \
           <(sed "$noversion" "$data/tinyleaf.expected") >/dev/null \
   && [ "$(ls "$tmp/spool")" = 42C972794A40A84CF8989D1F44D87648 ]; then
    res "tinyleaf HELP/IHAVE/duplicate/QUIT" PASS
else
    res "tinyleaf HELP/IHAVE/duplicate/QUIT" FAIL
fi

# INN tap tests, built against the CMake libraries.
libs=("$build/storage/libinnstorage.a" "$build/history/libinnhist.a"
      "$build/lib/libinn.a" -lz -lsqlite3)
cflags=(-g -I"$root/include" -I"$root/tests" -I"$root/history"
        -I"$root/storage")
cc "${cflags[@]}" -c tests/tap/basic.c -o "$tmp/basic.o" 2>"$tmp/basic.log"
# Each entry: test-name[:source-without--t.c[:extra-cflag]]
tests=(
    lib/hissqlite lib/bloom-hiswalk expire/tombstone-hisexpire
    overview/api
    overview/buffindexed:overview/overview:-DOVTYPE=buffindexed
    overview/tradindexed:overview/overview:-DOVTYPE=tradindexed
    overview/ovsqlite
    overview/tdx-group overview/xref
    storage/caf storage/cancel-tombstone
)
for spec in "${tests[@]}"; do
    IFS=: read -r t src extra <<<"$spec"
    src=${src:-$t}
    n=$(echo "$t" | tr / _)
    if ! cc "${cflags[@]}" $extra -c "tests/$src-t.c" -o "$tmp/$n-t.o" \
            2>"$tmp/$n.log" \
       || ! c++ "$tmp/$n-t.o" "$tmp/basic.o" "${libs[@]}" -o "$tmp/$n.t" \
                2>>"$tmp/$n.log"; then
        res "test $t" "FAIL (build)"
        continue
    fi
    out=$(cd tests && C_TAP_SOURCE=$PWD C_TAP_BUILD=$PWD "$tmp/$n.t" 2>&1)
    rc=$?
    plan=$(echo "$out" | grep -m1 -E '^1\.\.' | cut -c4-)
    ok=$(echo "$out" | grep -cE '^ok ')
    if [ $rc = 0 ] && [ -n "$plan" ] && [ "$ok" = "$plan" ]; then
        res "test $t" "PASS ($ok/$plan)"
    else
        res "test $t" "FAIL ($ok/${plan:-?}, exit $rc)"
    fi
done

if [ $fail = 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
    exit 1
fi
