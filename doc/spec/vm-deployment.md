# INN++ deployment to local virtual machines: specification

Status:
- **Linux:** implemented (2026-11-04) by `support/vm-linux-install-checks.sh`
  and `support/vm/linux-vm-checks.sh`. 29/29 checks pass on Debian 13.
- **FreeBSD:** designed, not built (section 6).

## 1. Purpose

Deploy INN++ onto a real operating system install: its own kernel, its
own init system, cron, and reboots. Check that it installs, starts,
runs its daily maintenance and survives a restart as a production
server would. Docker can't show this (Docker spec, section 6).

## 2. Scope

- Linux with systemd, the primary Linux deployment target.
- FreeBSD with rc.d.
- Both run locally on the developer's Mac. AWS was considered and set
  aside by the architect, because local is free and fast and needs no cloud credentials.

## 3. Requirements

All Docker requirements apply (R1–R11): no root on the host, a
production build, the owners, modes and setuid rules, the first-install
data, and a reinstall that keeps configuration. In addition:

V1. Each run boots a fresh VM from the distribution's official cloud
    image and deletes it afterwards. There is no hand-built VM state, and
    `-k` keeps the VM for debugging.
V2. The guest is isolated from the host:
    - no host directories are mounted
    - no guest ports are forwarded to the host
    - the source goes in as a copied archive
V3. The server is managed by the OS's init system, not started by hand:
    a systemd unit on Linux, an rc.d script on FreeBSD.
V4. innd runs as news under the init system. The unit grants no extra
    privileges (no `AmbientCapabilities`), and port 119 is bound only
    through `innbind`.
V5. The service is hardened, and the hardening must not break INN. On
    systemd:
    - `ProtectSystem=full`
    - `ProtectHome`
    - `PrivateTmp`
    - `ProtectControlGroups`
    - write access limited to the INN prefix via `ReadWritePaths`
V6. `news.daily` succeeds when run as news, and innd keeps running
    through it. A crontab entry for news is installed.
V7. With the service enabled, innd is running again after a reboot, as
    news, serving port 119, and `innbind` is still root:news 4550.
V8. Stopping the service stops innd.
V9. The run records what it tested: the guest OS name and version, the
    kernel and the architecture.
V10. A run tests either the working tree (tracked files plus new,
     non-ignored files) or, with `-c`, only HEAD.

## 4. Host tooling

- Lima 2.2 (`limactl`) for Linux guests. It runs VMs as an ordinary account through
  Apple's Virtualization.framework. It's started in `--plain` mode, which
  turns off mounts, port forwarding and containerd (V2).
- QEMU 11 (`qemu-system-aarch64`) with the edk2 UEFI firmware, for
  FreeBSD guests, since Lima runs only Linux guests.
- Both are installed by the architect (`brew install lima qemu`). UTM stays
  available for looking at a VM interactively, but scripts don't depend
  on it.

## 5. Linux design (implemented)

### 5.1 Guest

Lima's `template:debian-13`:
- Debian 13 trixie
- arm64
- kernel 6.12 cloud build
- 4 CPUs and 4 GiB of memory

### 5.2 Steps (`support/vm-linux-install-checks.sh`)

1. Delete any leftover VM named `inn-check-linux`. Start a fresh one in
   plain mode, with Lima's log going to a temporary file.
2. Archive the source on the host (`git archive HEAD`, or the working
   tree without macOS file metadata). Copy it in with `limactl copy`.
3. Print the guest OS, kernel and architecture (V9).
4. **provision**: the Dockerfile's toolchain, so the two targets build
   alike:
   - the same Debian packages, plus cron
   - CMake 4.3 or later from PyPI
   - Boost 1.92 headers, verified against the Dockerfile's pinned
     SHA-256 (read from the Dockerfile, so there's one source of truth)
5. Run the Docker install checks unchanged:
   `support/docker/linux-install-checks.sh`, 17 checks.
6. **before-reboot**:
   - Install `innd.service` and start it.
   - Check that it's active, runs as news and serves port 119.
   - Run `news.daily delayrm` as news, and check that innd is still up.
   - Install the news crontab.
   - Enable the unit.
7. Restart the VM with `limactl stop` then `start`.
8. **after-reboot**:
   - Check that the unit is active and innd runs as news on port 119.
   - Check that innbind is still 4550.
   - `systemctl stop`, then check that innd is gone.
9. Delete the VM. On failure, print the end of innd's journal.

### 5.3 The systemd unit

It's adapted from `contrib/sample.init.systemd`:

| Setting | Sample | VM unit | Why |
|---|---|---|---|
| `Type` | `notify` | `forking` | sd_notify needs systemd support, not yet a build option (item 8) |
| Paths | `/usr/lib/news`, `/etc/news` | `/usr/local/news/...` | the CMake default prefix |
| `PIDFile` | `/run/news/innd.pid` | `<prefix>/run/innd.pid` | INN's `pathrun` |
| `AmbientCapabilities` | `CAP_NET_BIND_SERVICE` | none | innd binds through innbind (V4) |
| `ReadWritePaths` | `/var/spool/news` | `<prefix>` | `ProtectSystem=full` makes `/usr` read-only, prefix included |
| `NoNewPrivileges` | off | off | it would block setuid innbind |

The `ReadWritePaths` row is a real finding a container can't produce.
Without it, the sample's hardening makes a default-prefix install
unable to write its own database.

### 5.4 Checks

- 17 Docker install checks (Docker spec, section 4.5).
- 7 before-reboot checks: the service is active, runs as news, serves
  port 119; news.daily succeeds and innd survives it; the crontab is
  installed; the unit is enabled.
- 5 after-reboot checks: the service is active, runs as news, serves
  port 119; innbind is still 4550; stop works.
- 29 in total. A run takes about 3.5 minutes.

## 6. FreeBSD design (planned)

- Guest: the official FreeBSD arm64 `BASIC-CLOUDINIT` image, booted with
  `qemu-system-aarch64` and Apple's hypervisor (`-accel hvf`). The
  firmware is edk2, and a NoCloud seed ISO sets up the user and SSH key.
- Networking: QEMU user-mode networking, with only SSH forwarded, and
  only to the host's 127.0.0.1 on a random port (V2).
- Provision: `pkg install` for the toolchain. Boost 1.92 and CMake 4.3
  come from pkg if the ports are new enough, otherwise from the same
  pinned sources.
- Accounts: FreeBSD's base system has `news` (uid 8). Use it.
- Init: an rc.d script `innd`, with `innd_enable` set in `rc.conf`.
  `service innd start` must run `rc.news` as news. There's no systemd
  hardening to translate. Consider `daemon(8)`, and stay within the
  default securelevel.
- Checks:
  - the same 17 install checks, ported where they use GNU
    `stat`/`find`/`ps` options (FreeBSD's differ)
  - the same before-reboot, after-reboot and stop checks, with
    `service` in place of `systemctl`
- Expected to turn up portability fixes (CLAUDE.md item 10). Those get
  their own commits, tests first.

## 7. Toward a deployable systemd install

systemd is the deployment target for Linux, which these VM checks lead
toward:
- Install an `innd.service` with `cmake --install`, generated from the
  prefix, instead of writing it in the test.
- Build with libsystemd when found (item 8), for `Type=notify`.
- Possibly a timer unit for news.daily, as the systemd-native
  replacement for the crontab.

## 8. Open items

- FreeBSD (section 6).
- The installed systemd unit (section 7).
- x86_64 guests: slow under emulation on Apple Silicon. Use AWS or CI if
  they're ever needed.
- SELinux (a Fedora/RHEL guest), since Debian's AppArmor doesn't confine
  INN by default.
