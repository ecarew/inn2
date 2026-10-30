#!/bin/bash
#
#  Runs inside the container started by support/docker-linux-checks.sh, as
#  the ordinary user "tester" (not the news user), in a fresh copy of the
#  source in the current directory: what someone who clones the repository
#  does on Linux.  Configure and build with CMake alone (no Autoconf or
#  Make), then run support/verify-cxx.sh on a normal build and
#  support/live-innd.sh on a build for the tester's own account.

set -e
step() { echo "=== $*"; }

step "CMake build (build)"
cmake -S . -B build -G Ninja >/tmp/cmake.log 2>&1 \
    || { tail -30 /tmp/cmake.log; exit 1; }
cmake --build build >/tmp/build.log 2>&1 \
    || { grep -B12 -m3 -E 'error' /tmp/build.log | head -60; exit 1; }

step "CMake build (build-live, for $(id -un))"
cmake -S . -B build-live -G Ninja \
    -DINN_RUNAS_USER="$(id -un)" -DINN_RUNAS_GROUP="$(id -gn)" \
    >/tmp/cmake-live.log 2>&1 || { tail -30 /tmp/cmake-live.log; exit 1; }
cmake --build build-live >/tmp/build-live.log 2>&1 \
    || { grep -B12 -m3 -E 'error' /tmp/build-live.log | head -60; exit 1; }

# Run both even if the first fails; report the worse result.
status=0
step "support/verify-cxx.sh build"
support/verify-cxx.sh build || status=1
step "support/live-innd.sh build-live"
support/live-innd.sh build-live || status=1
exit $status
