#!/usr/bin/env bash
#
#  The full check before a push (the architect, 2026-11-17): what each
#  commit runs (support/verify-cxx.sh), plus the live innd test, which
#  covers INN's input and output end to end: NNTP feeding, routing, what
#  lands in the spool, history and overview, readers, feeds and filters.
#  Iced checks (support/compat-checks.sh, ctest -L iced, live-innd.sh
#  --iced) are not run.
#
#  Usage: support/prepush.sh
#
#  Needs build/ and build-live/ (with the Perl and Python filters), as
#  CLAUDE.md, "Testing", configures them.

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
fail=0

echo "== support/verify-cxx.sh build"
support/verify-cxx.sh build || fail=1

if ! grep -q '^INN_PERL_FILTERS:BOOL=ON' build-live/CMakeCache.txt 2>/dev/null \
    || ! grep -q '^INN_PYTHON_FILTERS:BOOL=ON' build-live/CMakeCache.txt; then
    echo "build-live is missing or lacks the filters; configure it with"
    echo "  cmake -S . -B build-live -G Ninja -DINN_RUNAS_USER=\$(id -un) \\"
    echo "    -DINN_RUNAS_GROUP=\$(id -gn) -DINN_PERL_FILTERS=ON -DINN_PYTHON_FILTERS=ON"
    exit 1
fi
echo "== support/live-innd.sh build-live"
if cmake --build build-live -j8 >/dev/null; then
    support/live-innd.sh build-live || fail=1
else
    echo "build-live failed to build"
    fail=1
fi

if [ $fail = 0 ]; then
    echo "PREPUSH: PASS"
else
    echo "PREPUSH: FAIL"
    exit 1
fi
