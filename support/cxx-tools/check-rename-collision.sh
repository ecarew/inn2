#!/bin/bash
#
#  Before renaming an identifier, report any function that already uses the
#  new name, where the rename would collide or silently shadow.
#
#  Usage: support/cxx-tools/check-rename-collision.sh <git-path> <old> <new>
#     e.g. check-rename-collision.sh storage/cnfs/cnfs.c private priv
#
#  Reads the committed (HEAD) version of the file, so run it before the
#  rename is committed.  Prints the functions that use both names.

path=$1 old=$2 new=$3
[ -n "$new" ] || { sed -n '4,10p' "$0"; exit 2; }
git show "HEAD:$path" | awk -v old="$old" -v new="$new" '
    /^[a-zA-Z_][a-zA-Z_0-9]*\(/ { fn = $1 }
    /^}/ { fn = "" }
    {
        if ($0 ~ "(^|[^a-zA-Z0-9_.>])" old "([^a-zA-Z0-9_]|$)" \
            && $0 !~ /^[[:space:]]*(\/\*|\*)/) o[fn] = 1
        if ($0 ~ "(^|[^a-zA-Z0-9_])" new "([^a-zA-Z0-9_]|$)") n[fn] = 1
    }
    END {
        c = 0
        for (f in o) if (f in n) { print "COLLISION in " f; c++ }
        if (c == 0) print "no function uses both " old " and " new
        exit c > 0
    }'
