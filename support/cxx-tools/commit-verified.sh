#!/bin/bash
#
#  Commit only after the verification checks pass, with their results in the
#  message (CLAUDE.md, "Testing before commit").
#
#  Usage: support/cxx-tools/commit-verified.sh <subject> <body-file> [path...]
#
#  Stages the given paths (plus all modified tracked files with -u if none are
#  given), refuses if any change is left unstaged (restoring the index as it
#  was before the run whenever it does not commit), runs support/verify-cxx.sh
#  on build/ and, if configured, build-asan/, plus support/live-innd.sh on
#  build-live/ if configured, and commits with a Tested: section for each.
#  Uses your normal git identity; set COMMIT_TRAILER to add a trailer line
#  such as a Co-Authored-By.

tools=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$tools/../.." && pwd)
cd "$root" || exit 1
subject=$1 body=$2
shift 2 2>/dev/null
[ -n "$subject" ] && [ -f "$body" ] || { sed -n '4,14p' "$0"; exit 2; }

# If this run does not commit, put the index back as it was before it staged
# anything, so a refused run leaves nothing staged for the next one.  What was
# staged beforehand (e.g. convert-file.sh's git mv) stays staged.
index_before=$(git write-tree) || exit 1
committed=false
tmp=
finish() {
    $committed || git read-tree "$index_before"
    [ -n "$tmp" ] && rm -rf "$tmp"
}
trap finish EXIT

if [ $# -gt 0 ]; then
    git add -- "$@" || { echo "git add failed"; exit 1; }
else
    git add -u
fi
if git diff --cached --quiet; then
    echo "nothing staged to commit"
    exit 1
fi
if git status --porcelain --untracked-files=no | grep -qv '^[MADR] '; then
    echo "unstaged changes remain; not committing"
    git status --short --untracked-files=no
    exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/commit-verified.XXXXXX")
builds=(build)
[ -f build-asan/CMakeCache.txt ] && builds+=(build-asan)
for b in "${builds[@]}"; do
    if ! support/verify-cxx.sh "$b" >"$tmp/$b.txt" 2>&1; then
        echo "VERIFY FAILED ($b); not committing"
        grep -vE 'PASS|SKIP' "$tmp/$b.txt"
        exit 1
    fi
done
live=false
if [ -f build-live/CMakeCache.txt ]; then
    live=true
    # shellcheck disable=SC2015  # the failure block is for either step failing
    cmake --build build-live -j8 >"$tmp/build-live.log" 2>&1 \
        && support/live-innd.sh build-live >"$tmp/live.txt" 2>&1 \
        || { echo "LIVE INND TEST FAILED; not committing"; \
             grep -vE 'PASS' "$tmp/live.txt"; exit 1; }
fi

{
    echo "$subject"; echo; cat "$body"; echo
    for b in "${builds[@]}"; do
        echo "Tested (before commit, $(date +%F), support/verify-cxx.sh $b):"
        cat "$tmp/$b.txt"; echo
    done
    if $live; then
        echo "Tested (before commit, $(date +%F), support/live-innd.sh build-live):"
        cat "$tmp/live.txt"; echo
    fi
    [ -n "$COMMIT_TRAILER" ] && echo "$COMMIT_TRAILER"
} | git commit -q -F - || exit 1
committed=true
git log -1 --format='%h %s'
