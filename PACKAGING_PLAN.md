# Packaging plan: downloadable INN++ for Linux, macOS and FreeBSD

Status: started 2026-11-16, after the CMake-only phase
(AUTOTOOLS_PLAN.md) and the v2.8.0-cxx.alpha.4 tag.

## Decisions (the architect, 2026-11-16)

- **Linux:** a `.deb` for Debian and Ubuntu, and a generic prebuilt
  tarball of `/usr/local/news` for other glibc systems.  No `.rpm` for
  now.
- **macOS:** a Homebrew tap (`ecarew/homebrew-inn2`) whose formula builds
  from the release tarball.  No signed `.pkg` (it would need a paid Apple
  Developer ID).
- **Where:** GitHub Actions builds the Linux packages and the macOS
  bottle on each release tag and attaches them to the GitHub release.
  FreeBSD is built locally in the QEMU VM.
- **FreeBSD:** a port first (`net/inn2-cxx`), tested in the local VM;
  `pkg` packages come from it, and it could later go to the ports tree.

## Constraints

- Security rules hold in every package: innbind is the only setuid-root
  file; no INN program runs as root; packages never set INN_RUNAS_*;
  the unit and anything in system directories is owned by root.
- Toolchain: CMake 4.3+ and Boost 1.92+.  Debian testing and unstable
  have both; Debian 13 (stable) has neither, so the `.deb` is built in
  Debian testing and declares that.  Homebrew and FreeBSD ports have
  current versions.
- Release builds come from the `dist` tarball, not a Git checkout.

## Steps

1. **Reproducible version string.**  `support/mkversion` uses the last
   commit's date for a prerelease, or today's date without Git, so a
   tarball build differs by day.  It honours `SOURCE_DATE_EPOCH` first
   (Debian sets it from the changelog), then Git, then today.
2. **The `.deb`** (`packaging/debian/`, built by
   `support/mkdeb` in a Debian testing container):
   - FHS layout as in install.pod: `/usr/lib/news` (prefix),
     `/etc/news`, `/var/lib/news`, `/var/spool/news`, `/var/log/news`,
     `/run/news`, `/usr/share/man`, `/usr/share/doc/inn2-cxx`, unit in
     `/usr/lib/systemd/system`.  Perl and Python filters, TLS, systemd,
     Cancel-Lock on.
   - Package `inn2-cxx`, Conflicts/Replaces `inn2` (Debian's INN 2).
   - Staged with `DESTDIR` as a non-root user; ownership and innbind's
     4550 set in the maintainer scripts (`postinst`): the `news`
     account Debian already has (uid 9), configuration files as
     conffiles, first-install data only when absent.
   - Checks: `lintian`; install the `.deb` in a clean container (owners,
     innbind the only setuid file, inn.conf paths); in the Lima VM,
     install and `systemctl start inn`.
3. **Generic tarball:** the `/usr/local/news` tree staged with DESTDIR,
   with an install script that creates the news user, sets owners and
   innbind's mode, and refuses to run as anyone but root.  Built on the
   oldest glibc in use (Debian testing for now; noted as a limit).
   *Done:* `support/mktarball`, `packaging/tarball/`.  OpenSSL and
   libcanlock are linked statically (the architect, 2026-11-17), since
   Fedora already ships OpenSSL 4 under a new soname; an OpenSSL security
   release means a new tarball.  No Perl or Python filters.  Install
   tested on Debian testing, Ubuntu devel and Fedora rawhide (install,
   rerun as upgrade keeping site files); two builds are identical.
4. **Release workflow** (`.github/workflows/release.yaml`): on a `v*`
   tag, build the `dist` tarball, the `.deb` and the generic tarball for
   amd64 and arm64, run the package checks, and attach everything to the
   GitHub release with SHA-256 sums.
5. **Homebrew tap:** a formula building from the release tarball, with
   `-DINN_PREFIX=$(brew --prefix)/var/inn` style paths kept out of the
   Cellar; caveats print the root steps (news user, innbind setuid),
   since Homebrew does not run as root.  Test: `brew install --build-from-source`,
   `brew test`, `brew audit --strict`.  Then a real sudo install on
   macOS to `~/news` (still owed from the install phase).
6. **FreeBSD port** in the QEMU VM: the port Makefile, pkg-plist, the
   rc.d script and the news user (FreeBSD's `news` uid 8); blocklist
   tested there; `poudriere testport` or `make stage check-plist
   package`.  Also closes the "a BSD" item.

## Open questions

- Package name: `inn2-cxx` until the architect decides otherwise (a
  name distinct from Debian's `inn2`).
- Signing release artifacts (GPG or Sigstore): later.
