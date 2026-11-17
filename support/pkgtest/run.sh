#!/bin/sh
#
#  Run a package install check in clean containers.
#
#      support/pkgtest/run.sh deb <dir-with-.deb> [image...]
#      support/pkgtest/run.sh tarball <dir-with-tarball> [image...]
#
#  Default images: the .deb on Debian testing; the tarball on Debian
#  testing, Ubuntu devel and Fedora rawhide.  Exits non-zero if any fails.
set -u
kind=$1
dir=$(cd "$2" && pwd)
shift 2
here=$(cd "$(dirname "$0")" && pwd)
case $kind in
deb)     script=deb-install.sh; [ $# -gt 0 ] || set -- debian:testing ;;
tarball) script=tarball-install.sh
         [ $# -gt 0 ] || set -- debian:testing ubuntu:devel fedora:rawhide ;;
*)       echo "usage: run.sh deb|tarball dir [image...]" >&2; exit 2 ;;
esac
rc=0
for img in "$@"; do
    echo "=== $kind on $img"
    docker run --rm -v "$dir:/pkg:ro" -v "$here/$script:/t.sh:ro" "$img" \
        sh /t.sh || rc=1
done
exit $rc
