#!/bin/bash
#
#  Rename one C source to C++ and build it: the first step of converting a
#  file (CLAUDE.md, "Conversion workflow").  Does not commit.
#
#  Usage: support/cxx-tools/convert-file.sh <dir> <path-in-dir-without-.c>
#     e.g. support/cxx-tools/convert-file.sh storage cnfs/cnfs
#
#  Renames <dir>/<path>.c to .cc with git mv, updates <dir>/CMakeLists.txt,
#  renames the C++ keywords "class" and "private" in code (not comments or
#  strings; see fixkw.pl), builds, and prints the compiler errors left to fix.
#  Warns if the file already used "priv", which the private rename targets.

tools=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$tools/../.." && pwd)
cd "$root" || exit 1
d=$1 r=$2
[ -n "$d" ] && [ -n "$r" ] || { sed -n '4,13p' "$0"; exit 2; }
[ -f "$d/$r.c" ] || { echo "no such file: $d/$r.c" >&2; exit 1; }

git mv "$d/$r.c" "$d/$r.cc" || exit 1
esc=$(printf '%s' "$r" | sed 's|/|\\/|g; s|\.|\\.|g')
sed -i '' -E "s/(^|[[:space:](])$esc\.c([[:space:]]|\)|$)/\1$esc.cc\2/g" \
    "$d/CMakeLists.txt"
grep -qE "(^|[[:space:](])$esc\.cc([[:space:]]|\)|$)" "$d/CMakeLists.txt" \
    || echo "WARNING: $d/CMakeLists.txt does not list $r.cc; update it by hand"

perl "$tools/fixkw.pl" "$d/$r.cc" class class_ storageclass
if grep -qw priv "$d/$r.cc"; then
    echo "WARNING: $r.cc already uses 'priv'; checking for collisions"
    bash "$tools/check-rename-collision.sh" "$d/$r.c" private priv
fi
perl "$tools/fixkw.pl" "$d/$r.cc" private private_ priv

log=$(mktemp "${TMPDIR:-/tmp}/convert-file.XXXXXX")
if cmake --build build -j8 >"$log" 2>&1; then
    echo "BUILD OK"
else
    echo "BUILD FAILED: $(grep -c 'error:' "$log") errors"
fi
grep -E 'error:' "$log" | sed "s|$root/||" | head -40
rm -f "$log"
