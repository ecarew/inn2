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

# The LLVM tools matching the compiler: Xcode's through xcrun on macOS, else
# the ones on PATH (a coverage build needs clang; CMakeLists.txt, INN_COVERAGE).
llvm() {
    if command -v xcrun >/dev/null 2>&1; then
        xcrun "$@"
    else
        "$@"
    fi
}

llvm llvm-profdata merge -sparse "$covdir"/raw/*.profraw \
    -o "$covdir/merged.profdata" || exit 1

# Every executable in the build (innd, tinyleaf, the other INN programs)
# plus the test programs verify-cxx.sh kept.
objects=("$build/innd/innd")
while IFS= read -r o; do
    [ "$o" = "$build/innd/innd" ] || objects+=(-object "$o")
done < <(find "$build" -path "$covdir" -prune -o -type f -perm -u+x \
             ! -name '*.sh' ! -name '*.dylib' -print \
         | while IFS= read -r f; do
               file "$f" | grep -q 'Mach-O.*executable' && echo "$f"
           done)
for o in "$covdir"/bin/*.t; do
    objects+=(-object "$o")
done
ignore='(^|/)tests/|^/Applications/|^/Library/|^/usr/|/build[^/]*/'

# The repository path may contain regex characters (e.g. "inn++"); escape it.
rootre=$(printf '%s' "${root#/}" | sed 's/[][\.*^$+?(){}|/]/\\&/g')
llvm llvm-cov report -instr-profile="$covdir/merged.profdata" \
    -ignore-filename-regex="$ignore" "${objects[@]}" \
    | sed -E "s|/?$rootre/||"

if $html; then
    llvm llvm-cov show -format=html -output-dir="$covdir/html" \
        -instr-profile="$covdir/merged.profdata" \
        -ignore-filename-regex="$ignore" "${objects[@]}" >/dev/null \
        && echo "HTML report: $covdir/html/index.html"
fi
