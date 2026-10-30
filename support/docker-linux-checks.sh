#!/bin/bash
#
#  Build INN++ and run its checks on Linux, as an ordinary user inside a
#  Docker container (Debian trixie): support/verify-cxx.sh and
#  support/live-innd.sh, from a fresh copy of the source, with CMake alone.
#
#  Usage: support/docker-linux-checks.sh [-c]
#
#  By default the tracked files are streamed in, including uncommitted
#  changes to them (new files once staged).  With -c, only the last commit
#  is used: exactly what someone cloning the repository gets.  The root-path
#  checks are support/docker-root-checks.sh.  Needs a running Docker daemon;
#  the first run builds the image inn-linux-root-checks
#  (support/docker/Dockerfile.linux).

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
image=inn-linux-root-checks

docker build -q -t "$image" -f support/docker/Dockerfile.linux \
    support/docker >/dev/null || { echo "image build failed" >&2; exit 1; }

if [ "${1:-}" = -c ]; then
    tree=HEAD
else
    # The current index plus the working tree's changes to tracked files, in
    # a temporary index; the real index and working tree are not touched.
    idx=$(mktemp "${TMPDIR:-/tmp}/docker-linux-checks.XXXXXX")
    trap 'rm -f "$idx"' EXIT
    cp "$(git rev-parse --git-path index)" "$idx"
    tree=$(GIT_INDEX_FILE=$idx git add -u && GIT_INDEX_FILE=$idx git write-tree) \
        || { echo "cannot build the source tree" >&2; exit 1; }
fi
git archive --format=tar "$tree" \
    | docker run --rm -i "$image" bash -c \
        'mkdir /src && tar -x -C /src && chown -R tester /src && cd /src \
         && su tester -c "bash support/docker/linux-checks.sh"'
