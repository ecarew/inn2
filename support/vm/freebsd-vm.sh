#!/bin/bash
#
#  A local FreeBSD arm64 VM under QEMU on macOS (Apple silicon), for the
#  FreeBSD port and BSD portability checks (CLAUDE.md, item 10).
#
#      support/vm/freebsd-vm.sh start    boot it (first run: set it up)
#      support/vm/freebsd-vm.sh ssh [cmd]
#      support/vm/freebsd-vm.sh stop
#
#  Uses the FreeBSD 15.0 BASIC-CLOUDINIT image, downloaded and checked
#  against FreeBSD's CHECKSUM.SHA256 into $FREEBSD_VM_DIR (default
#  ~/.local/share/inn-freebsd).  The first start copies it to disk.qcow2
#  (grown to 40 GB) and makes a cloud-init seed: user "inn" with a key
#  made here (id_ed25519) and passwordless doas.  SSH is forwarded to
#  127.0.0.1:2222.  Needs Homebrew's qemu (the user installs it).
set -eu

dir=${FREEBSD_VM_DIR:-$HOME/.local/share/inn-freebsd}
image=$dir/FreeBSD-15.0-RELEASE-arm64-aarch64-BASIC-CLOUDINIT-ufs.qcow2
port=2222
fw=$(brew --prefix qemu)/share/qemu/edk2-aarch64-code.fd
ssh_opts=(-i "$dir/id_ed25519" -p $port -o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)

setup() {
    [ -f "$image" ] || { echo "missing $image; download it first" >&2; exit 1; }
    [ -f "$dir/id_ed25519" ] || ssh-keygen -q -t ed25519 -N "" -f "$dir/id_ed25519"
    cp "$image" "$dir/disk.qcow2"
    qemu-img resize -q "$dir/disk.qcow2" 40G
    dd if=/dev/zero of="$dir/vars.fd" bs=1m count=64 2>/dev/null
    local seed=$dir/seed
    rm -rf "$seed" && mkdir -p "$seed"
    printf 'instance-id: inn-freebsd\nlocal-hostname: inn-freebsd\n' \
        >"$seed/meta-data"
    cat >"$seed/user-data" <<EOF
#cloud-config
users:
  - name: inn
    groups: wheel
    shell: /bin/sh
    ssh_authorized_keys:
      - $(cat "$dir/id_ed25519.pub")
packages:
  - doas
runcmd:
  - echo 'permit nopass :wheel' > /usr/local/etc/doas.conf
EOF
    rm -f "$dir/seed.iso"
    hdiutil makehybrid -quiet -iso -joliet -default-volume-name cidata \
        -o "$dir/seed.iso" "$seed"
}

case ${1:-} in
start)
    [ -f "$dir/disk.qcow2" ] || setup
    qemu-system-aarch64 -M virt,accel=hvf -cpu host -smp 4 -m 4096 \
        -drive if=pflash,format=raw,readonly=on,file="$fw" \
        -drive if=pflash,format=raw,file="$dir/vars.fd" \
        -drive if=virtio,format=qcow2,file="$dir/disk.qcow2" \
        -drive if=virtio,format=raw,media=cdrom,readonly=on,file="$dir/seed.iso" \
        -netdev user,id=n0,hostfwd=tcp:127.0.0.1:$port-:22 \
        -device virtio-net-pci,netdev=n0 \
        -display none -serial file:"$dir/console.log" \
        -daemonize -pidfile "$dir/qemu.pid"
    for _ in $(seq 1 90); do
        ssh "${ssh_opts[@]}" inn@127.0.0.1 true 2>/dev/null && { echo "up"; exit 0; }
        sleep 5
    done
    echo "no SSH after 7.5 minutes; see $dir/console.log" >&2
    exit 1
    ;;
ssh)
    shift
    exec ssh "${ssh_opts[@]}" inn@127.0.0.1 "$@"
    ;;
stop)
    ssh "${ssh_opts[@]}" inn@127.0.0.1 doas shutdown -p now 2>/dev/null || true
    sleep 10
    [ -f "$dir/qemu.pid" ] && kill "$(cat "$dir/qemu.pid")" 2>/dev/null || true
    rm -f "$dir/qemu.pid"
    ;;
*)
    echo "usage: freebsd-vm.sh start|ssh [cmd]|stop" >&2
    exit 2
    ;;
esac
