#!/bin/bash
#
#  Runs inside the container started by support/docker-root-checks.sh, as
#  the container's root, in a fresh copy of the source in the current
#  directory: build with CMake (no Autoconf or Make) for the newstest
#  account, and run support/root-checks.sh.

set -e
src=$(pwd)

step() { echo "=== $*"; }

step "CMake build"
cmake -S . -B build-linux -G Ninja \
    -DINN_RUNAS_USER=newstest -DINN_RUNAS_GROUP=newstest \
    >/tmp/cmake.log 2>&1 || { tail -30 /tmp/cmake.log; exit 1; }
cmake --build build-linux >/tmp/build.log 2>&1 \
    || { grep -B12 -m3 -E 'error' /tmp/build.log | head -60; exit 1; }

step "support/root-checks.sh build-linux"
"$src/support/root-checks.sh" build-linux
