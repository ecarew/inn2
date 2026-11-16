# Plan: retiring Autoconf and Make

The CMake-only goal (decided 2026-10-28, CLAUDE.md): no Autoconf or Make
left in the tree. Scheduled by the architect on 2026-11-13, after the
Asio work; moved ahead of the ovdb migration guide on 2026-11-15.

## Where things stand (survey, 2026-11-15)

- **The Autoconf build is already dead.** The Makefiles still name the
  `.c` sources (`innd/Makefile`: `art.c cc.c ...`) that became `.cc` in
  the conversion, so `./configure && make` cannot build INN. Deleting the
  Autoconf files loses no working build.
- **Nothing builds through configure.** The Docker checks (Linux, root,
  install), the perf scripts and `commit-verified.sh` use CMake only;
  `support/docker/Dockerfile.linux` installs no Autoconf.
- **CMake already does what make did** for configuration, generated
  sources, scripts (`inn_fixscript`), samples, filters, the Perl modules,
  install rules and tests. It still uses two scripts from `support/`:
  `mksystem` (inn/system.h) and `mkversion` (inn/version.h, given
  `project(INN VERSION ...)`); both stay.
- **The one real gap is CMake-only goal item 6:** man pages and the text
  documents made from POD. Only `doc/pod/Makefile` (a maintainer target)
  runs pod2man and pod2text, and none of the results are in git, so a
  CMake install today ships no man pages and no INSTALL, NEWS or HACKING.

## Decisions (the architect, 2026-11-15)

1. **Man pages are built at build time.** CMake runs pod2man on
   `doc/pod/*.pod` and on the scripts and Perl modules with embedded POD,
   and pod2text for the text documents, and installs them. Nothing
   generated is committed. pod2man comes with Perl, which INN needs.
2. **Release tarballs come from `git archive`,** through a CMake `dist`
   target that adds the generated text documents. `MANIFEST`,
   `support/mkmanifest` and `support/mksnapshot` are deleted: git is the
   file list.
3. **`support/getrra-c-util` and `support/getc-tap-harness` are
   deleted.** They copy m4 and C files from rra-c-util and C TAP Harness
   that INN++ has since converted or removed. Replacing the TAP harness
   is already on the backlog.

## Steps

Each step is its own commit (or a few), verified as usual, with the Linux
checks and the Linux install checks before the push.

1. **Man pages and documents in CMake** (`doc/CMakeLists.txt`).
   - pod2man with `-c 'InterNetNews Documentation' -r 'INN <version>'`,
     plus `--guesswork=none` where pod2man supports it (podlators 5.0+;
     configure probed for it, because Perl's guesswork mangles patterns
     such as `C<@@NCM>`).
   - Sections as `doc/pod/Makefile` gave them (1, 3, 5, 8), plus the
     embedded-POD pages: delayer(8), mailpost(8), perl-nocem(8),
     pgpverify(1), and INN::Config, INN::Utils::Shlock and
     INN::ovsqlite_client (3pm). auth_krb5(8) only when auth_krb5 is
     built. Pages for deleted code (libinn_list, libinn_tst, send-uucp)
     are dropped.
   - Installed under `${prefix}/share/man` (Autoconf's default mandir),
     with `doc/man/Makefile`'s links: hisv6.5, motd.innd.5,
     motd.nnrpd.5, localgroups.5, nocem.ctl.5, actsyncd.8, imapfeed.8,
     inpaths.8, sendxbatches.8.
   - Text documents (INSTALL, NEWS, HACKING, checklist, external-auth,
     hook-perl, hook-python) built with pod2text and installed to
     docdir with the other documents `doc/Makefile` installed.
   - Tests: a CTest check that runs podchecker on every POD source, and
     the Linux install checks gain checks for the man pages, the links
     and the documents.
2. **`dist` target:** `support/mkdist` makes `inn-<version>.tar.gz` from
   `git archive` of HEAD plus the generated text documents; a CMake
   custom target runs it.
3. **Delete the Autoconf build:** `configure.ac`, `autogen`, `m4/`,
   `Makefile.global.in`, every `Makefile` (22, including `site/` with
   `site/getsafe.sh`), `support/config.guess`, `config.sub`,
   `install-sh`, `ltmain.sh`, `makedepend`, `fixconfig.in`,
   `fixscript.in`, `compare-config.sh` (it compared CMake's config.h with
   configure's), `mkmanifest`, `mksnapshot`, `getrra-c-util`,
   `getc-tap-harness`, and `MANIFEST`. Tidy `.gitignore` and
   `.codespellrc`. Comments in `cmake/` that cite an m4 file as the
   source of a check say "was m4/<file>" instead.
4. **Documentation:** install.pod's build and configure sections
   describe CMake (BUILDING.md has the details); hacking.pod,
   checklist.pod and the other pages that mention `./configure`,
   `make install` or `make check` are updated; the CLAUDE.md goal is
   closed.

## Done (2026-11-16)

All four steps are done: man pages and documents (7bb7ec3ab), `dist`
(b9f7a0518), the Autoconf build removed (78d9ba263), and the
documentation (install.pod 65aef6b59, then checklist, hissqlite,
inn.conf, nnrpd, readers.conf, hacking.pod, BUILDING.md, README.md,
TODO, CLAUDE.md).  Added on the way, at the architect's request: the
directory options and INN_NEWS_USER/GROUP (3ea348028); INN_SYSTEMD with
inn.service, INN_BLOCKLIST and INN_TAGGED_HASH (2cb9ef494, so the
follow-ups below are done); the `cert` target (5d39dfac7).  The GitHub
workflow runs the CMake checks (afd1bb1fd, 7ef5201c7).

Still not ported: `make reformat` and `make code-check` (clang-format,
perltidy, black, shfmt, codespell, perlcritic); Berkeley DB/ovdb (its
migration guide is next); large files.

## Not in this phase (follow-ups)

configure had two optional features that the CMake build leaves off
(`cmake/InnConfigure.cmake` sets them to false), lost already with the
dead Makefiles rather than by this phase:

- **systemd** (`sd_notify`, `HAVE_SD_NOTIFY`), for the systemd unit.
- **blocklist** (`HAVE_BLOCKLIST`/`HAVE_BLACKLIST`), for blocklistd.

Each becomes a CMake option that finds the library, as Kerberos and SASL
do, as part of CMake-only goal item 8.
