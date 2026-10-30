#!/bin/bash
#
#  Build and run support/bench/header-lookup.cc: innd's header-name lookup,
#  today's tree against a sorted array.  The table is innd's ARTheaders[]
#  from innd/innd.cc; the mix is every header line of the test articles in
#  tests/data/articles plus support/bench/header-mix.txt, a typical modern
#  Usenet header block.  Not part of INN or of the verification.
#
#  Usage: support/bench/header-lookup.sh [compiler flags, default -O2]

root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

grep -o 'ARTHEADERINIT("[^"]*"' "$root/innd/innd.cc" \
    | sed 's/.*("//; s/"$//' >"$tmp/names"
{
    for f in "$root"/tests/data/articles/[0-9]*; do
        # Header lines only: up to the first empty line, not continuations.
        sed -n '/^\r\{0,1\}$/q; /^[^ 	][^:]*:/s/:.*//p' "$f"
    done
    cat "$root/support/bench/header-mix.txt"
} >"$tmp/mix"

flags=("$@")
[ ${#flags[@]} -eq 0 ] && flags=(-O2)
c++ -std=c++20 "${flags[@]}" "$root/support/bench/header-lookup.cc" \
    -o "$tmp/header-lookup" || exit 1
"$tmp/header-lookup" "$tmp/names" "$tmp/mix"
