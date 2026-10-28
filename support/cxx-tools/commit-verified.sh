#!/bin/bash
#
#  Commit only after the verification checks pass, with their results in the
#  message (CLAUDE.md, "Testing before commit").
#
#  Usage: support/cxx-tools/commit-verified.sh <subject> <body-file> [path...]
#
#  Stages the given paths (plus all modified tracked files with -u if none are
#  given), refuses if any change is left unstaged, runs support/verify-cxx.sh
#  on build/ and, if configured, build-asan/, and commits with a Tested:
#  section for each.  Uses your normal git identity; set COMMIT_TRAILER to add
#  a trailer line such as a Co-Authored-By.

tools=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$tools/../.." && pwd)
cd "$root" || exit 1
subject=$1 body=$2
shift 2 2>/dev/null
[ -n "$subject" ] && [ -f "$body" ] || { sed -n '4,13p' "$0"; exit 2; }

if [ $# -gt 0 ]; then
    git add -- "$@" || { echo "git add failed"; exit 1; }
else
    git add -u
fi
if git status --porcelain --untracked-files=no | grep -qv '^[MADR] '; then
    echo "unstaged changes remain; not committing"
    git status --short --untracked-files=no
    exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/commit-verified.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
builds=(build)
[ -f build-asan/CMakeCache.txt ] && builds+=(build-asan)
for b in "${builds[@]}"; do
    if ! support/verify-cxx.sh "$b" >"$tmp/$b.txt" 2>&1; then
        echo "VERIFY FAILED ($b); not committing"
        grep -vE 'PASS|SKIP' "$tmp/$b.txt"
        exit 1
    fi
done

{
    echo "$subject"; echo; cat "$body"; echo
    for b in "${builds[@]}"; do
        echo "Tested (before commit, $(date +%F), support/verify-cxx.sh $b):"
        cat "$tmp/$b.txt"; echo
    done
    [ -n "$COMMIT_TRAILER" ] && echo "$COMMIT_TRAILER"
} | git commit -q -F - && git log -1 --format='%h %s'
