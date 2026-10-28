#!/bin/bash
#
#  Apply one clang-tidy check's automatic fixes to .cc files only, then
#  clang-format the files it changed.  Does not commit.
#
#  Usage: support/cxx-tools/tidy-fix.sh <dir-regex> <check>
#     e.g. support/cxx-tools/tidy-fix.sh '(innd|lib)' modernize-use-nullptr
#
#  Headers are excluded (-exclude-header-filter), and the script stops if
#  anything other than the matching .cc files changed.  Review the diff
#  before committing: clang-tidy 23.1.4's modernize-use-using fix corrupts
#  anonymous struct typedefs (CONVERSION_NOTES.md).  Needs a configured build/
#  with compile_commands.json and Homebrew LLVM.

L=${LLVM_BIN:-/opt/homebrew/opt/llvm/bin}
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root" || exit 1
d=$1 check=$2
[ -n "$check" ] || { sed -n '4,13p' "$0"; exit 2; }
[ -z "$(git status --porcelain --untracked-files=no)" ] \
    || { echo "working tree not clean"; exit 1; }

"$L/run-clang-tidy" -p build -clang-tidy-binary "$L/clang-tidy" \
    -clang-apply-replacements-binary "$L/clang-apply-replacements" \
    -exclude-header-filter='.*' -quiet -j 8 -fix -checks="-*,$check" \
    "/$d/.*\.cc\$" >/dev/null 2>&1
changed=$(git diff --name-only)
outside=$(echo "$changed" | grep -vE "^$d/.*\.cc\$" | grep .)
if [ -n "$outside" ]; then
    echo "UNEXPECTED changes outside $d/*.cc (review, or git restore):"
    echo "$outside"
    exit 1
fi
for f in $changed; do "$L/clang-format" --style=file -i "$f"; done
echo "files changed: $(echo "$changed" | grep -c .)"
git diff --stat | tail -1
