# INN++ Deployment to a Developer's Docker Container: Specification

Status: describes the deployment as built (2026-11-04).  Implemented by
`support/docker/Dockerfile.linux`, `support/docker-install-checks.sh` and
`support/docker/linux-install-checks.sh`.

## 1. Purpose

Install INN++ the way an administrator would (`cmake --install` as root)
on Linux, and prove that the result matches what `make install` produced
for INN 2.8.0 in ownership, modes and behaviour.  This must run on a
developer's Mac with no root access on the host.

## 2. Scope

In scope: building, installing as root, first-install data, and starting
the server once.  Out of scope: init systems, cron, reboots, kernel
security modules (see the VM spec), and a published image (CLAUDE.md
open item 12).

## 3. Requirements

R1. Nothing runs as root on the host, so no sudo is needed.
R2. The host's files are never mounted into the container.  The source
    goes in as a stream, and the container cannot write back.
R3. The build is a production build with no `INN_RUNAS_USER` or
    `INN_RUNAS_GROUP` set.  A test build must refuse to install.
R4. The install runs as root.  Every installed file is owned by
    `news:news`, except `innbind`, which is `root:news` mode 4550.
R5. `innbind` is the only setuid file.  No file is setgid unless
    `INN_SETGID_INEWS` is on, and then only `inews` (2555).
R6. The modes match the Makefile install macros (XPRI 0550, XPUB 0555,
    RPRI 0640, RPUB 0644, inews 0550, rnews 0500).  `run/` is 0750.
R7. A first install creates `active`, `newsgroups`, `active.times` and an
    empty history database (dbz, via `makedbz -i -o`), owned by news.
R8. The server is started the way an administrator starts it: `rc.news`
    as the news user.  innd runs as news, never root, and binds port 119
    through `innbind`.
R9. A reinstall keeps configuration and data, and `innbind` stays setuid.
R10. Each run starts from a clean container.  Nothing persists between
     runs except the toolchain image.
R11. The result is machine-readable: one line per check, then
     `RESULT: PASS` or `RESULT: FAIL`, with a non-zero exit on failure.

## 4. Design

### 4.1 Image (toolchain only)

- Base: `debian:trixie` (Debian 13), on the host's architecture (arm64
  on Apple Silicon).
- Debian packages: the compiler, Ninja, Perl, Python 3, flex, bison,
  SQLite, zlib, and procps/netcat for the checks.  Kerberos, Dovecot
  and libcanlock are also installed for the other Docker check scripts,
  which share this image.
- CMake 4.3 or later from PyPI, in a private venv at `/opt/cmake`,
  because Debian's CMake is too old.
- Boost 1.92 headers, built from the official release tarball. The
  tarball is verified against a SHA-256 pinned in the Dockerfile
  (`ARG BOOST_SHA256`), because Debian's 1.83 is too old.
- Accounts: Debian's base image already has `news:news`. The image adds
  `newstest` and `tester`, used by the other Docker checks.
- The image holds no INN source and no INN build.

### 4.2 Source delivery

1. The host wrapper builds a git tree from a temporary index. That index
   is the current index plus working-tree changes to tracked files, so
   new files are included once staged. The developer's real index is not
   touched.
2. `git archive` of that tree is piped into `docker run --rm -i`. The
   container unpacks it into `/src`.

### 4.3 Build and install (in the container, as root)

1. Create the `news` user and group if they're absent.
2. Configure: `cmake -S . -B build -G Ninja -DINN_TESTS=OFF`, with the
   default prefix `/usr/local/news`.
3. Build: `cmake --build build -j$(nproc)`.
4. Install: `cmake --install build`.  The install rules then:
   - create the directories (mode 0755; `run/` 0750)
   - install the programs, with configuration files only if absent
   - chown the install manifest and the new directories to news
   - set `innbind` to root:news 4550, after the chown
   - create the first-install data and history
5. Set `domain:` in `inn.conf`, because the container's hostname has no
   domain part.

### 4.4 Run

- `su news -c rc.news`, then wait until `ctlinnd mode` answers (up to
  10 s).
- Read the banner on 127.0.0.1:119. Accept either 200 or 201.
- `ctlinnd shutdown`.

### 4.5 Checks

There are 17 checks, mapped to R4–R9. In outline:
- install success
- ownership
- innbind's owner and mode
- setuid and setgid scans
- per-program modes
- `run/` mode
- the history database
- the server running as news
- the port 119 banner
- reinstall keeping an edited `inn.conf`, and innbind still setuid
- an `INN_SETGID_INEWS=ON` build in `/opt/news-sg` giving inews 2555

## 5. Security considerations

- The container runs as root, but only inside the container, and with
  no host mounts (R1, R2).
- The setuid scan covers the whole prefix, not a fixed list. A new
  setuid file therefore fails the check.
- The toolchain downloads are limited to two sources: Debian's mirrors
  and PyPI for CMake (not pinned to a hash; open item), and Boost's
  release archive, pinned by SHA-256.

## 6. Limitations (why the VM spec exists)

- There is no init system: innd is started by hand. systemd units and
  rc.d scripts are not exercised.
- There is no cron, syslog or reboot.
- The container shares the host kernel and runs under Docker's
  capability set and seccomp profile, not a distribution's
  AppArmor/SELinux or default limits.
- Port 119 is reached only inside the container.

## 7. Open items

- Pin CMake by version and hash, as Boost is.
- A published image (item 12) needs its own spec:
  - a runtime image without the toolchain
  - innd as news, never a test build
  - persistent volumes for `db/`, `spool/`, `log/` and `etc/`
  - publishing approved by the architect
