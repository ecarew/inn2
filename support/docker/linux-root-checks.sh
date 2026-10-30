#!/bin/bash
#
#  Runs inside the container started by support/docker-root-checks.sh, as
#  the container's root, in a fresh copy of the source in the current
#  directory: run autogen and configure (still needed for samples/inn.conf,
#  CMake-only goal item 3), build with CMake for the newstest account, and
#  run support/root-checks.sh.  CMake generates its own config.h and other
#  headers and sources; configure's copies are not used.

set -e
src=$(pwd)

step() { echo "=== $*"; }

step "autogen and configure"
./autogen >/tmp/autogen.log 2>&1 || { tail -30 /tmp/autogen.log; exit 1; }
# configure only records the sendmail path; the container has none.
./configure --with-news-user=newstest --with-news-group=newstest \
    --with-sendmail=/usr/sbin/sendmail \
    >/tmp/configure.log 2>&1 || { tail -30 /tmp/configure.log; exit 1; }

step "samples/inn.conf (Make)"
make -C samples inn.conf >/tmp/make.log 2>&1 || { tail -30 /tmp/make.log; exit 1; }
step "CMake build"
cmake -S . -B build-linux -DINN_RUNAS_USER=newstest -DINN_RUNAS_GROUP=newstest \
    >/tmp/cmake.log 2>&1 || { tail -30 /tmp/cmake.log; exit 1; }
cmake --build build-linux -j"$(nproc)" >/tmp/build.log 2>&1 \
    || { grep -B12 -m3 -E 'error' /tmp/build.log | head -60; exit 1; }

step "support/root-checks.sh build-linux"
"$src/support/root-checks.sh" build-linux
