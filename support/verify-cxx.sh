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
# Under /tmp rather than $TMPDIR: macOS's $TMPDIR is long enough that
# ovsqlite-integ's server socket path in the sandbox passes the 104-byte
# sun_path limit, and ovsqlite-server truncates it silently.
tmp=$(mktemp -d /tmp/verify-cxx.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
fail=0

res() {
    printf '  %-42s %s\n' "$1" "$2"
    [[ "$2" == PASS* || "$2" == SKIP* ]] || fail=1
}

cd "$root" || exit 1

# A build configured with -DINN_COVERAGE=ON: build the tests with coverage
# too, send every run's profile to $build/coverage/raw, and keep the test
# programs in $build/coverage/bin for support/coverage-report.sh.
covdir=
covlink=()
covcflags=()
if grep -q '^INN_COVERAGE:BOOL=ON' "$build/CMakeCache.txt" 2>/dev/null; then
    covdir=$build/coverage
    rm -rf "$covdir"
    mkdir -p "$covdir/raw" "$covdir/bin"
    export LLVM_PROFILE_FILE="$covdir/raw/%m-%p.profraw"
    covlink=(-fprofile-instr-generate)
    covcflags=(-fprofile-instr-generate -fcoverage-mapping)
fi

if cmake --build "$build" -j8 >"$tmp/build.log" 2>&1; then
    res "cmake build (lib, history, storage, innd)" PASS
else
    res "cmake build (lib, history, storage, innd)" FAIL
    tail -20 "$tmp/build.log" >&2
    echo "RESULT: FAIL"
    exit 1
fi

# Every C++ file must compile as C++20 (CLAUDE.md, "Build system"); per-target
# settings once left most of the tree on the compiler default without notice.
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

# Every symbol the Makefile-built innd exports (innd-symbols.txt, Mach-O
# names) must still be exported, under its C name, by the CMake build.
defined_symbols "$build/innd/innd" | awk '{print $2}' | sort >"$tmp/syms"
# __mh_execute_header is the Mach-O header symbol Apple's linker adds to
# every executable, not one of INN's.
sed 's/^_//' "$data/innd-symbols.txt" | grep -vx '_mh_execute_header' \
    | sort >"$tmp/want-syms"
want=$(wc -l <"$tmp/want-syms" | tr -d ' ')
missing=$(comm -23 "$tmp/want-syms" "$tmp/syms" | wc -l | tr -d ' ')
if [ "$missing" = 0 ]; then
    res "innd exports all $want Make-build symbols" PASS
else
    res "innd exports all $want Make-build symbols" "FAIL ($missing missing)"
fi

mangled=0
for a in "$build/innd/innd" "$build"/history/libinnhist.a \
         "$build"/storage/libinnstorage.a "$build"/lib/libinn.a; do
    n=$(defined_symbols "$a" 2>/dev/null | awk '$1 == "T" && $2 ~ /^_Z/' | wc -l)
    mangled=$((mangled + n))
done
if [ "$mangled" = 0 ]; then
    res "C linkage kept (no mangled exports)" PASS
else
    res "C linkage kept (no mangled exports)" "FAIL ($mangled mangled)"
fi

# innd checks its user and group first, against RUNASUSER/RUNASGROUP compiled
# in.  A normal build (news) stops there on a machine without a news group; a
# build for the current user (INN_RUNAS_USER) gets past it to reading inn.conf.
runas=$(sed -n 's/^#define RUNASUSER "\(.*\)"/\1/p' "$build/include/config.h")
if [ "$runas" = "$(id -un)" ]; then
    expect="open of .*inn.conf failed"
else
    # Without a news account innd cannot resolve it (macOS); with one, as
    # on Debian, it gets one step further and refuses the tester.
    expect="can't resolve $runas to a [GU]ID|must be run as $runas"
fi
if "$build/innd/innd" -h 2>&1 | grep -qE "$expect"; then
    res "innd starts (passes or reaches user check)" PASS
else
    res "innd starts (passes or reaches user check)" FAIL
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

# convdate (expire/) has no INN test.  Check each conversion in UTC.
cv() { TZ=UTC INNCONF=$root/tests/data/etc/inn.conf "$build/expire/convdate" "$@" 2>&1; }
d='Wed, 28 Oct 2026 12:00:00 +0000'
if [ "$(cv -n "$d")" = 1793188800 ] \
   && [ "$(cv -c 1793188800)" = "Wed Oct 28 12:00:00 2026" ] \
   && [ "$(cv -s "$d")" = "Wed Oct 28 12:00:00 2026" ] \
   && [ "$(cv "$d")" = "Wed Oct 28 12:00:00 2026" ] \
   && [ "$(cv -d "$d")" = "Wed, 28 Oct 2026 12:00:00 -0000 (UTC)" ] \
   && [ "$(cv -dl "$d")" = "Wed, 28 Oct 2026 12:00:00 +0000 (UTC)" ] \
   && ! cv -n 'not a date' >/dev/null; then
    res "convdate conversions (7 cases)" PASS
else
    res "convdate conversions (7 cases)" FAIL
fi

# gencancel (control/) has no INN test.  Check the cancel articles it writes
# for several option sets; Date and Message-ID vary per run and are masked.
gencancel_runs() {
    local g=$1
    export INNCONF=$root/tests/data/etc/inn.conf
    "$g" -n test.group -f admin@live.test -s 'cancel test' -b 'Body.' \
        '<orig@live.test>'
    echo "--"; "$g" -a -n test.group -f admin@live.test '<orig@live.test>'
    echo "--"; "$g" -m -c UTF-8 -n test.group -f admin@live.test '<o@l.t>'
    echo "--"; "$g" -d -n a.b,c.d -f admin@live.test '<o@l.t>'
    echo "--"; "$g" -k -n x '<a@b>' 2>&1 | grep -v inn-secrets; echo "exit $?"
    echo "--"; "$g" -L -n x '<a@b>' 2>&1 | grep -v inn-secrets; echo "exit $?"
}
mask='s/^(Date|Message-ID): .*/\1: (masked)/'
gencancel_runs "$build/control/gencancel" 2>/dev/null | sed -E "$mask" \
    >"$tmp/gencancel.out"
g=$tmp/gencancel.out
if grep -q '^Control: cancel <orig@live.test>$' "$g" \
   && grep -q '^Subject: cancel test$' "$g" \
   && grep -q '^Subject: cmsg cancel <o@l.t>$' "$g" \
   && grep -q '^Newsgroups: a.b,c.d$' "$g" \
   && grep -q '^Content-Type: text/plain; charset="UTF-8"$' "$g" \
   && [ "$(grep -c '^Approved: ' "$g")" = 3 ] \
   && [ "$(grep -c '^Message-ID: ' "$g")" = 3 ] \
   && [ "$(grep -c 'not built with Cancel-Lock support' "$g")" = 2 ] \
   && grep -q '^Body\.$' "$g"; then
    res "gencancel output (6 option sets)" PASS
else
    res "gencancel output (6 option sets)" FAIL
fi
# Differential check against the original C gencancel from main, built with
# the original Autoconf/Make libraries when they are present.
if [ -f lib/.libs/libinn.a ] && [ -f storage/.libs/libinnstorage.a ] \
   && git show main:control/gencancel.c >"$tmp/gencancel-orig.c" 2>/dev/null \
   && cc -g -I"$root/include" -c "$tmp/gencancel-orig.c" -o "$tmp/gco.o" \
        2>"$tmp/gco.log" \
   && cc "$tmp/gco.o" lib/.libs/libinn.a history/.libs/libinnhist.a \
        storage/.libs/libinnstorage.a lib/.libs/libinn.a lib/canlock.o \
        -lz -lsqlite3 -o "$tmp/gencancel-orig" 2>>"$tmp/gco.log"; then
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

# INN tap tests, built against the CMake libraries.
# libinnstorage and libinnhist use each other.  Apple's ld resolves symbols
# in any order, but GNU ld only searches forward, so name libinnstorage again
# after libinnhist (as history/CMakeLists.txt makes CMake do).
libs=("$build/innd/libinnd_core.a" "$build/storage/libinnstorage.a"
      "$build/history/libinnhist.a" "$build/storage/libinnstorage.a"
      "$build/lib/libinn.a" -lz -lsqlite3)
cflags=(-g -I"$build/include" -I"$root/include" -I"$root/tests" -I"$root/history"
        -I"$root/storage")
cflags+=("${covcflags[@]}")
# A build configured with -DINN_SANITIZE=ON needs its test programs built
# with the same sanitizer flags.
sanflags=()
if grep -q '^INN_SANITIZE:BOOL=ON' "$build/CMakeCache.txt" 2>/dev/null; then
    # shellcheck disable=SC2054  # the comma is part of the -fsanitize value
    sanflags=(-fsanitize=address,undefined -fno-omit-frame-pointer
              -fno-sanitize-recover=undefined)
    cflags+=("${sanflags[@]}")
fi
tap=()
for h in basic float messages process string; do
    cc "${cflags[@]}" -c "tests/tap/$h.c" -o "$tmp/tap-$h.o" 2>>"$tmp/tap.log"
    tap+=("$tmp/tap-$h.o")
done

# Compile one extra source into a test.  lib/X entries are the file under
# test, built with -DTESTING as tests/Makefile does, from X.cc with the C++
# compiler once converted, else X.c; anything else is plain C test support.
extra_obj() {
    local src=$1 out=$2
    case "$src" in
    lib/*)
        if [ -f "$src.cc" ]; then
            c++ -std=c++20 "${cflags[@]}" -DTESTING -c "$src.cc" -o "$out"
        else
            cc "${cflags[@]}" -DTESTING -c "$src.c" -o "$out"
        fi ;;
    *)  if [ -f "$src.cc" ]; then
            c++ -std=c++20 "${cflags[@]}" -c "$src.cc" -o "$out"
        else
            cc "${cflags[@]}" -c "$src.c" -o "$out"
        fi ;;
    esac
}

# Each entry (a name ending in .t is a shell test, run as is):
# test-name[:source-without--t.c[:extra-cflag[:extra-sources]]]
# with extra-sources separated by commas.  An extra source starting with "@" is an
# object already built in the build directory.  innd tests link innd_core
# with tests/innd/fakeinnd.c in place of innd.cc, as tests/Makefile does.
# shellcheck disable=SC2054  # commas separate an entry's extra sources
tests=(
    lib/artnumber lib/asprintf:::lib/asprintf lib/bloom lib/bloom-hiswalk
    lib/buffer lib/canlock lib/concat lib/conffile lib/confparse lib/date
    lib/daemon:::lib/daemon lib/dispatch lib/fdflag
    lib/getaddrinfo:::lib/getaddrinfo lib/getnameinfo:::lib/getnameinfo
    lib/hash lib/hashtab lib/headers lib/hex lib/hissqlite
    lib/hissqlite-convert lib/inet_aton:::lib/inet_aton
    lib/inet_ntoa:::lib/inet_ntoa lib/inet_ntop:::lib/inet_ntop lib/innconf
    lib/list lib/md5 lib/messageid lib/messages lib/mkstemp:::lib/mkstemp
    lib/newsuser
    lib/network/addr-ipv4 lib/network/addr-ipv6 lib/network/client
    lib/network/server lib/pread:::lib/pread lib/pwrite:::lib/pwrite lib/qio
    lib/readin lib/reallocarray:::lib/reallocarray lib/reservedfd
    lib/setenv:::lib/setenv lib/snprintf:::lib/snprintf
    lib/strlcat:::lib/strlcat lib/strlcpy:::lib/strlcpy lib/tst lib/uwildmat
    lib/vector lib/wire lib/xwrite:::lib/xwrite,tests/lib/fakewrite
    authprogs/ident authprogs/identport authprogs/radius authprogs/ckpasswd.t
    authprogs/domain.t
    nnrpd/auth-ext:::nnrpd/auth-ext
    innd/artparse:::tests/innd/fakeinnd,@lib/CMakeFiles/inn_canlock.dir/canlock.cc.o,@lib/CMakeFiles/inn_perl.dir/perl.cc.o
    innd/chan:::tests/innd/fakeinnd,@lib/CMakeFiles/inn_canlock.dir/canlock.cc.o,@lib/CMakeFiles/inn_perl.dir/perl.cc.o
    innd/headers:::tests/innd/fakeinnd,@lib/CMakeFiles/inn_canlock.dir/canlock.cc.o,@lib/CMakeFiles/inn_perl.dir/perl.cc.o
    expire/tombstone expire/tombstone-e2e.t
    expire/tombstone-hisexpire
    overview/api
    overview/buffindexed:overview/overview:-DOVTYPE=buffindexed
    overview/tradindexed:overview/overview:-DOVTYPE=tradindexed
    overview/ovsqlite overview/ovsqlite-integ.t
    overview/tdx-group overview/tdx-util.t overview/xref
    storage/caf storage/cancel-tombstone storage/methods
    storage/makehistory.t storage/sm.t storage/archive.t
)
# buffindexed creates a System V shared memory segment and never removes it
# (same in the original C), and macOS allows only 32.  Remove unattached
# segments a test created; leave any that existed before it ran.
shm_ids() { ipcs -m 2>/dev/null | awk '$1 == "m" { print $2 }' | sort; }
shm_cleanup() {
    local id
    for id in $(comm -13 "$1" <(shm_ids)); do
        ipcs -m -a 2>/dev/null | awk -v id="$id" '$1 == "m" && $2 == id { exit $(NF-6) != 0 }' \
            && ipcrm -m "$id" 2>/dev/null
    done
}

# INN's program tests (authprogs, and the shell tests) run programs as
# ../../<dir>/<prog> relative to tests/data, the Make build's in-tree layout.  Give them a sandbox with a
# real copy of tests/data (a symlink would resolve ../.. from its target)
# and authprogs pointing at the CMake-built programs.
sb=$tmp/sandbox
mkdir -p "$sb/tests" && cp -R tests/data "$sb/tests/data"
for d in authprogs backends control expire frontends innfeed nnrpd samples storage; do
    [ -d "$build/$d" ] && ln -s "$build/$d" "$sb/$d"
done

# lib/hissqlite-convert runs the program by this path; its default is the
# Make build's location.
export HISSQLITE_CONVERT=$build/history/hissqlite-convert

# overview/ovsqlite-integ.t starts this server and runs two helper programs,
# which it expects as overview/ovsqlite-{write,read}.t beside it, and reads
# ../include/config.h to see whether SQLite is built in.
export OVSQLITE_SERVER=$build/storage/ovsqlite-server
ln -s "$build/include" "$sb/include"
mkdir -p "$sb/tests/overview"
for h in write read; do
    cc "${cflags[@]}" -c "tests/overview/ovsqlite-$h-t.c" -o "$tmp/ovsqlite-$h.o" \
        2>>"$tmp/ovsqlite-helpers.log" \
    && c++ "$tmp/ovsqlite-$h.o" "${tap[@]}" "${libs[@]}" -lm "${sanflags[@]}" \
           "${covlink[@]}" -o "$sb/tests/overview/ovsqlite-$h.t" \
           2>>"$tmp/ovsqlite-helpers.log"
done

base_cflags=("${cflags[@]}")
for spec in "${tests[@]}"; do
    shm_ids >"$tmp/shm.before"
    IFS=: read -r t src extra srcs <<<"$spec"
    # macOS's gethostbyname() returns hostent data at misaligned addresses,
    # which UBSan flags in these two tests (in the test and in the unconverted
    # C fallback); turn off only the alignment check for them.
    cflags=("${base_cflags[@]}")
    case "$t" in
    lib/getaddrinfo|lib/getnameinfo)
        [ ${#sanflags[@]} -gt 0 ] && cflags+=(-fno-sanitize=alignment) ;;
    esac
    shell=false
    if [[ "$t" == *.t ]]; then
        shell=true
        t=${t%.t}
    fi
    rundir=tests
    # lib/innconf reads ../../samples/inn.conf from tests/data: in the
    # sandbox that is the build's generated samples/inn.conf.
    { [[ "$t" == authprogs/* ]] || [ "$t" = lib/innconf ] || $shell; } \
        && rundir=$sb/tests
    src=${src:-$t}
    n=$(echo "$t" | tr / _)
    objs=()
    built=true
    IFS=, read -ra extras <<<"$srcs"
    for e in "${extras[@]}"; do
        if [[ "$e" == @* ]]; then
            objs+=("$build/${e#@}")
            continue
        fi
        o="$tmp/$n-$(basename "$e").o"
        extra_obj "$e" "$o" 2>>"$tmp/$n.log" || built=false
        objs+=("$o")
    done
    if $shell; then
        out=$(cd "$rundir" && C_TAP_SOURCE=$PWD C_TAP_BUILD=$PWD \
              sh "$root/tests/$t.t" 2>&1)
        rc=$?
    elif ! $built \
       || ! cc "${cflags[@]}" ${extra:+"$extra"} -c "tests/$src-t.c" \
            -o "$tmp/$n-t.o" \
            2>>"$tmp/$n.log" \
       || ! c++ "$tmp/$n-t.o" "${objs[@]}" "${tap[@]}" "${libs[@]}" -lm "${sanflags[@]}" "${covlink[@]}" \
                -o "$tmp/$n.t" 2>>"$tmp/$n.log"; then
        res "test $t" "FAIL (build)"
        continue
    else
        [ -n "$covdir" ] && cp "$tmp/$n.t" "$covdir/bin/"
        out=$(cd "$rundir" && C_TAP_SOURCE=$PWD C_TAP_BUILD=$PWD "$tmp/$n.t" 2>&1)
        rc=$?
    fi
    shm_cleanup "$tmp/shm.before"
    plan=$(echo "$out" | grep -m1 -E '^1\.\.' | cut -c4-)
    # INN's older tests print a bare count as their first line (e.g. "7").
    [ -z "$plan" ] && plan=$(echo "$out" | head -1 | grep -E '^[0-9]+$')
    ok=$(echo "$out" | grep -cE '^ok ')
    if [[ "$plan" == "0 # skip"* ]]; then
        res "test $t" "SKIP (${plan#0 # skip })"
    elif [ "$t" = authprogs/ident ] && [ "$rc" = 0 ] && [ "$plan" = 4 ] \
         && ! nc -z 127.0.0.1 113 >/dev/null 2>&1 \
         && [ "$(echo "$out" | grep -E '^(not )?ok ' | tr '\n' ' ')" \
              = "ok 1 not ok 2 ok 3 not ok 4 " ]; then
        # Tests 2 and 4 ask an identd on port 113, which this host lacks
        # (macOS runs none); the same two fail with the original C ident.
        res "test $t" "PASS (2/4; 2 need an identd on port 113)"
    elif [ "$rc" = 0 ] && [ -n "$plan" ] && [ "$ok" = "$plan" ]; then
        res "test $t" "PASS ($ok/$plan)"
    else
        res "test $t" "FAIL ($ok/${plan:-?}, exit $rc)"
        # Show what the test printed, minus its passing lines, so an
        # intermittent failure leaves something to diagnose.
        echo "$out" | grep -vE '^ok ' | head -30 | sed 's/^/    | /'
    fi
done

if [ $fail = 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
    exit 1
fi
