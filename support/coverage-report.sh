#!/bin/bash
#
#  Code coverage report for the INN++ C++ conversion.  Build with coverage and
#  run the verification checks first:
#
#      cmake -S . -B build-cov -DINN_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
#      support/verify-cxx.sh build-cov
#      support/coverage-report.sh build-cov [--html]
#
#  Prints a per-file summary of INN's own sources (tests excluded).  With
#  --html, also writes a line-by-line report to build-cov/coverage/html/.
#  Uses Xcode's llvm-profdata/llvm-cov, which match Apple clang's profile
#  format (Homebrew LLVM's newer tools may not).

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/${1:-build-cov}
covdir=$build/coverage
html=false
[ "$2" = --html ] && html=true

ls "$covdir"/raw/*.profraw >/dev/null 2>&1 || {
    echo "no profiles in $covdir/raw; run support/verify-cxx.sh $1 first" >&2
    exit 1
}

xcrun llvm-profdata merge -sparse "$covdir"/raw/*.profraw \
    -o "$covdir/merged.profdata" || exit 1

objects=("$build/innd/innd")
for o in "$build/innd/tinyleaf" "$covdir"/bin/*.t; do
    objects+=(-object "$o")
done
ignore='(^|/)tests/|^/Applications/|^/Library/|^/usr/|/build[^/]*/'

xcrun llvm-cov report -instr-profile="$covdir/merged.profdata" \
    -ignore-filename-regex="$ignore" "${objects[@]}" \
    | sed "s|$root/||"

if $html; then
    xcrun llvm-cov show -format=html -output-dir="$covdir/html" \
        -instr-profile="$covdir/merged.profdata" \
        -ignore-filename-regex="$ignore" "${objects[@]}" >/dev/null \
        && echo "HTML report: $covdir/html/index.html"
fi
