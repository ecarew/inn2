#!/bin/bash
#
#  Add static_casts for the compiler's "void * to T" errors in one file.
#
#  Usage: support/cxx-tools/autocast.sh <file>
#
#  Builds, reads errors of the form "cannot initialize a variable of type 'T'
#  with an lvalue of type 'void *'" (and the assigning-to forms) for <file>,
#  and rewrites each reported single-line "= EXPR;" as
#  "= static_cast<T>(EXPR);" with castrhs.pl.  Stops on any line it cannot
#  rewrite.  Review every change: it only handles single-line assignments.

tools=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$tools/../.." && pwd)
cd "$root" || exit 1
f=$1
[ -f "$f" ] || { sed -n '4,12p' "$0"; exit 2; }

args=()
while IFS=$'\t' read -r line type; do
    args+=("$line" "$type")
done < <(cmake --build build -j8 2>&1 \
    | grep -E "/$f:[0-9]+:[0-9]+: error: " \
    | perl -ne "print \"\$1\t\$2\n\" if m{:(\d+):\d+: error: (?:cannot initialize a variable of type|assigning to) '([^']+)' (?:\(aka '[^']+' \) )?(?:with an lvalue of type|from incompatible type|with an rvalue of type) '(?:const )?void \*'}" \
    | sort -n -u)
[ ${#args[@]} -gt 0 ] || { echo "no void * errors found in $f"; exit 0; }
perl "$tools/castrhs.pl" "$f" "${args[@]}"
