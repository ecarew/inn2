#!/bin/bash
# vm-linux-install-checks.sh [-c] [-k]: install INN++ as root in a fresh
# Linux VM (Lima, Debian 13) and check what a container cannot: the
# install checks of support/docker/linux-install-checks.sh, then innd
# started by systemd, and innd running again after a reboot.
#   -c  test only the last commit (git archive HEAD), not the working tree
#   -k  keep the VM afterwards (default: delete it)
# Needs limactl on the host.  Nothing on the host runs as root.
set -eu
cd "$(dirname "$0")/.."
ref=; keep=
while getopts ck o; do
    case $o in c) ref=HEAD ;; k) keep=1 ;; *) exit 2 ;; esac
done
vm=inn-check-linux
command -v limactl >/dev/null || { echo "limactl not found"; exit 2; }

cleanup() { [ -n "$keep" ] || limactl delete -f "$vm" >/dev/null 2>&1 || true; }
trap cleanup EXIT
limactl delete -f "$vm" >/dev/null 2>&1 || true

# Plain mode: no host mounts (the guest gets only the source archive) and
# no port forwarding, so the guest's news server is never reachable from
# the host's network.  Lima's own messages go to a log.
log=$(mktemp -t vm-linux-checks).log
echo "Lima log: $log"
limactl start --name="$vm" --tty=false --plain \
    --cpus=4 --memory=4 template:debian-13 >>"$log" 2>&1

tarball=$(mktemp -t innsrc).tar
if [ -n "$ref" ]; then
    git archive -o "$tarball" "$ref"
else
    # The working tree: tracked files plus new files not yet ignored.
    git ls-files -co --exclude-standard -z | tar --no-mac-metadata --no-xattrs --no-fflags \
        --null -cf "$tarball" -T -
fi
limactl copy "$tarball" "$vm:/tmp/src.tar"
rm -f "$tarball"

sh_vm() { limactl shell "$vm" sudo bash -c "$1"; }
sh_vm 'rm -rf /src && mkdir /src && tar -xf /tmp/src.tar -C /src'
sh_vm '. /etc/os-release; echo "Guest: $PRETTY_NAME, $(uname -srm)"'
sh_vm 'bash /src/support/vm/linux-vm-checks.sh provision'
sh_vm 'cd /src && bash support/docker/linux-install-checks.sh'
sh_vm 'bash /src/support/vm/linux-vm-checks.sh before-reboot'
limactl stop "$vm" >>"$log" 2>&1 && limactl start "$vm" >>"$log" 2>&1
sh_vm 'bash /src/support/vm/linux-vm-checks.sh after-reboot'
