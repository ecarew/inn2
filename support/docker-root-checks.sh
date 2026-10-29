#!/bin/bash
#
#  Run support/root-checks.sh on Linux, as root inside a Docker container:
#  no sudo on the host, and nothing from the host is mounted.  The tracked
#  files (including uncommitted changes to them, and new files once
#  staged) are streamed in with git archive, built with CMake for the
#  container's newstest account, and checked.
#
#  Usage: support/docker-root-checks.sh
#
#  Needs a running Docker daemon.  The first run pulls debian:trixie and
#  builds the image inn-linux-root-checks (support/docker/Dockerfile.linux).

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
image=inn-linux-root-checks

docker build -q -t "$image" -f support/docker/Dockerfile.linux \
    support/docker >/dev/null || { echo "image build failed" >&2; exit 1; }

# Build the tree to send in a temporary index: the current index plus the
# working tree's changes to tracked files.  New files are included once
# staged.  The real index and working tree are not touched.
idx=$(mktemp "${TMPDIR:-/tmp}/docker-root-checks.XXXXXX")
trap 'rm -f "$idx"' EXIT
cp "$(git rev-parse --git-path index)" "$idx"
tree=$(GIT_INDEX_FILE=$idx git add -u && GIT_INDEX_FILE=$idx git write-tree) \
    || { echo "cannot build the source tree" >&2; exit 1; }
git archive --format=tar "$tree" \
    | docker run --rm -i "$image" bash -c \
        'mkdir /src && tar -x -C /src && cd /src \
         && bash support/docker/linux-root-checks.sh'
