# INN++ conversion notes

INN++ is a conversion of [INN](https://github.com/InterNetNews/inn) 2.8.0
from C with an Autoconf/Make build to C++20 with a CMake-only build.  This
file records **what was decided, why, and what else was considered**, so that
someone reviewing the conversion can check each choice against the original
source, and either agree with it or follow exactly what was done.

Rules for working on the code (style, testing before commit, the open-items
list) are in [CLAUDE.md](CLAUDE.md).  This file is the rationale.

## Tracing a change back to the original

- **Original source:** branch `main` is upstream INN, unmodified (fork point
  `6843efceb`).  `upstream` is the original repository.
- **Conversion work:** branch `cmake-build`.  Every source file was converted
  in its own commit named `Compile <dir>/<file> as C++`, so
  `git log --follow -p <file>.cc` shows the original `.c` and each change made
  to it.  `git diff main -- <file>.c <file>.cc` shows the whole difference.
- **What was tested:** every commit after `eb6a81d2f` has a `Tested:` section
  listing the checks that ran, before the commit, and their results.  Earlier
  commits have the same information in `git notes` (`git log --notes`), marked
  as checked after the commit.  Fetch the notes with
  `git fetch origin refs/notes/commits:refs/notes/commits`.
- **Findings:** where a test or tool turned up something surprising, the
  commit message has a "Findings ... (for reviewers)" section.
- **Branches kept for reference:** `cxx-conversion-wip` (earlier hand edits,
  including a first `ArtHeader` class) and `legacy-2020` (a 2020 attempt).

## Method

- **Mechanical first, modernization second.**  Each file was first made to
  compile as C++ with the smallest change (casts, keyword renames), and only
  later modernized in separate, single-purpose commits (`nullptr`, `using`, ...).
  A reviewer can therefore read the conversion commits as "same behavior,
  different language".  Tools used to modernize included, but were not
  limited to, clang-tidy (`modernize-*` checks, applied to `.cc` files only)
  and clang-format (INN's `.clang-format` style), both from LLVM 23.1.4, ...
- **Verification before every commit** (`support/verify-cxx.sh`): build, the
  `innd` program still exports all 169 symbols of the original Make build,
  no C++ name mangling in the libraries' exported functions, `innd` starts,
  a `tinyleaf` NNTP session, and 65 of INN's own test programs (2 skip).  Also
  run on an AddressSanitizer + UBSan build.
- **Differential testing:** new tests were also run against the original
  Make-built C libraries.  When a test failed on both, the test was wrong, not
  the conversion (see `26928139d`, `b55ea92a1`).
- **Coverage is measured, not assumed** (`support/coverage-report.sh`).  As of
  2026-10-29: lib 77.3%, history 64.8%, storage 38.1%, **innd 42.3%** (2.0%
  originally; 9.5% with innd/artparse and innd/chan; 33.9% with the live innd
  test, support/live-innd.sh; 42.3% once it covered 22 more ctlinnd
  commands).  Code
  with no coverage is not refactored until it has tests.

## Decisions

### Build system

- **CMake only, no Autoconf/Make in the end state** (2026-10-28).  The
  remaining steps are listed in CLAUDE.md, "Goal: CMake-only build".  Until
  they are done, CMake reuses `./configure`'s `config.h` (`dc61d2089`).
- **Generated sources are C++ and built by CMake** (2026-10-30,
  `daa978cd1`-`f59b45640`).  The three Perl generators were changed to
  write `.cc` and to take an output directory, rather than having CMake
  compile `.c` output as C++, so each file's name says what it is.  CMake
  runs them from their `.in` files with `add_custom_command` (fixscript
  changes only the `#!` line), writes the output to the build tree, and
  declares the generator and its inputs (every `method.config`,
  `ovmethod.config`, `hismethod.config` and `.sql` file) as dependencies.
  Generated output must not be left in the source tree: a quoted
  `#include "methods.h"` finds a stale copy beside the including file
  before the build tree's.
- **C++20**, not C++23.  Neither standard is fully implemented in Clang/libc++
  (Apple's toolchain), but C++20 has only a few remaining gaps, none of
  which INN++ needs.  libc++ lists C++20 papers still partial: calendars and
  time zones (P0355R7), the execution-policy overloads of
  `shift_left`/`shift_right` (P0769R2), and parts of uses-allocator
  construction (P0591R4); `std::jthread`/`stop_token` (P0660R10) needs
  `-fexperimental-library`.  cppreference's table (which covers Apple Clang
  only up to version 16) shows modules and coroutines as partial and
  `std::osyncstream` and `jthread` missing.  C++23 has more gaps: about 25
  libc++ papers have no status, including `std::generator` (P2502R2) and
  stacktrace (P0881R7).  Avoid the features listed as incomplete (modules,
  chrono time zones, `jthread`, execution policies) until they are complete.
  Sources (read 2026-10-29): <https://libcxx.llvm.org/Status/Cxx20.html>,
  <https://libcxx.llvm.org/Status/Cxx23.html>,
  <https://en.cppreference.com/w/cpp/compiler_support/20>.  Apple publishes no
  per-feature table for Apple Clang, so the LLVM libc++ pages are the closest
  authoritative source; this project builds with Apple Clang 21.
- **The C++20 setting had not reached most of the code** (found 2026-10-29).
  It was requested per target, on the innd targets only, so 88 of the 108
  `.cc` files (all of lib, history and storage) were compiled with the
  compiler's default, C++14 on Apple Clang 21, and buffindexed_d with GNU
  extensions (`gnu++20`).  Found by checking the `-std` flag in
  `compile_commands.json` while converting gencancel.  Now set once for every
  target (`CMAKE_CXX_STANDARD 20`, required, extensions off); all `.cc` files
  compile as `-std=c++20`, and the whole tree builds and passes verification
  under C++20.  Earlier `Tested:` sections for lib, history and storage commits
  therefore describe C++14 builds of those files.
- **Assertions stay on in every build** (`13a7720aa`).  CMake's Release-type
  defaults add `-DNDEBUG`, which silently disabled INN's 28 `assert()` calls;
  configure's `-g -O2` never did.  `-DNDEBUG` is stripped.

### Keeping C and C++ code linkable during the conversion

- **C linkage through the headers, not the source files.**  INN's headers
  already wrap declarations in `BEGIN_DECLS`/`END_DECLS` (`extern "C"` under
  C++).  Missing guards were added to headers and to the scripts that generate
  headers (`49208f35e`, `fb1f8bd8c`), so converted `.cc` files include headers
  exactly as the C did.  This kept every exported name unchanged, which the
  symbol check confirms.
- **`include/inn/tst.h` was missing its `END_DECLS`** (`eba96dd22`): an
  upstream bug that is invisible in C, where both macros are empty.
- **`nnrpd/nnrpd.h` had no `BEGIN_DECLS`/`END_DECLS` at all** (`d298ec362`):
  nnrpd was all C, so nothing needed them.  Converting the first file
  (track.c) left the C files calling an unmangled `TrackClient` that the C++
  file no longer exported.  Everything after the header's includes is now
  wrapped, as in the innd headers.
- **`void *` from `xmalloc` and friends:** `include/inn/xmalloc.h` returns a
  small `inn_void_ptr` helper in C++ that converts to any pointer type, so the
  hundreds of allocation calls need no casts.  Every other `void *` conversion
  got an explicit `static_cast`.  Considered: casting every call site (too
  noisy for a mechanical step); `new`/`delete` (a behavior change, kept for
  modernization).  `inn_void_ptr` is temporary and should go when allocation
  moves to RAII.

### Names that are C++ keywords

- **Struct fields in headers that C still uses** (`TOKEN.class`,
  `ARTHANDLE.private` in `include/inn/storage.h`) get a C++-only name
  (`class_`, `private_`) under `#ifdef __cplusplus`.  Same layout; C code is
  untouched.  The 2020 attempt (`legacy-2020`) renamed them everywhere instead.
- **Fields in headers private to one library** were renamed outright:
  `STORAGE_SUB.class`, `CNFSEXPIRERULES.class` and `CNFSARTHEADER.class` became
  `storageclass` (`fb1f8bd8c`).  Layouts are unchanged, so existing CNFS
  spools stay readable.
- **Locals and parameters:** `class` → `storageclass`, `private` → `priv`
  (`privp` in `cnfs.cc`, which already had a `priv`), `this` → `smc`,
  `delete` → `dodelete`, `new` → a descriptive name (`entry`, `newcp`,
  `lowered`, `glued` in ctlinnd, ...).  Renames were applied to code only,
  never to comments or log strings, and checked function by function for
  name collisions.
- **Struct fields private to one program** were renamed the same way:
  frontends/sm's `struct options` field `delete` → `dodelete` (`f2b775f2f`).
- convert-file.sh renames only `class` and `private` automatically
  (support/cxx-tools/fixkw.pl); `new`, `delete` and `this` are renamed by
  hand, since a good replacement depends on what the name means.

### Other C/C++ differences that needed code changes

- An enum declared inside a struct is file-scope in C but struct-scope in C++;
  `lib/date` hoists it to `enum rule_type` (`5fdb4cd86`).
- C++ does not convert integers to enums implicitly: `lib/nntp` casts
  `strtol()` to `enum nntp_code` (`d413525d6`).
- C's tentative definitions (`FILE *Log;` followed by `FILE *Log = NULL;`) are
  redefinitions in C++ (`eb1e5e2eb`).  nnrpd hit this through its `EXTERN`
  macro, which is empty in nnrpd.c: `Tracing` and `hasSentCapabilities` were
  declared `EXTERN` but initialized in nnrpd.c, against the header's own rule
  that initialized globals use plain `extern`.  They now follow that rule
  (`ffdf0db35`).
- `strchr` on a `const char *` returns `const char *` in C++.  In nnrpd's
  posting path (`CMDpost`, commands.cc) the reply from `ARTpost` (declared
  `const char *`) is truncated at its CR/LF in place.  This is safe only
  because a CR or LF comes from innd's reply, which `ARTpost` copies into
  post.c's writable static `Error` buffer, and never from the string
  literals it also returns.  The writes now go through `const_cast` with a
  comment saying so (`1ff1892a4`).  Behavior unchanged; making the result
  an owned string is a refactoring candidate (CLAUDE.md).
- In C++, Python's `PyMODINIT_FUNC` already expands to
  `extern "C" PyObject *`, so nnrpd.h's `extern PyMODINIT_FUNC` repeated
  `extern`.  The redundant one is dropped (`5952b3822`).
- The same `const_cast` pattern in backends/archive (`3f1f556df`): the Xref
  groups come from `cvector_split_space`, whose strings are `const char *`,
  and each is ended at its colon in place.  They point into the function's
  own `xstrndup` copy of the header, so the write is safe; a comment says
  so.
- Programs that link no INN library (`NOLIBS` in CMake, e.g. ninpaths)
  allocate with their own helpers or `calloc`, which return plain `void *`;
  `inn_void_ptr` does not apply, so those results get a `static_cast`
  (`812a465b0`).
- `static const` objects need an initializer in C++ (`= {}`, same zero value).
- Test-only prototypes under `TESTING` need C linkage, or the C tests cannot
  link (`6a8df6188`).

### Code compiled out in this build

Some files are almost entirely inside a feature test that is off here
(`HAVE_OPENSSL`, `DO_PERL`, `DO_PYTHON`, `HAVE_BDB`), so building them as C++
proves nothing.  Each such conversion also ran a syntax-only compile
(`c++ -fsyntax-only -std=c++20`) with the feature defined and the library's
headers on the include path; the commits give the exact commands.
- nnrpd's `tls.cc`, `perl.cc` and `python.cc`: the check found six `void *`
  conversions in python.cc that the normal build could not see.  For Perl,
  only the CORE directory was used (`-idirafter`): the full
  `perl -MExtUtils::Embed -e ccopts` adds MacPorts' `/opt/local/include`,
  whose headers clash with the system ones INN was configured against.
- frontends' ovdb tools, against Homebrew's Berkeley DB 18 (installed
  2026-10-29): seven `void *` conversions in ovdb_server.cc and
  ovdb_stat.cc (`57e431e0b`, `74499a1fa`).  The Berkeley DB headers must
  come first (`-I`, not `-idirafter`): the macOS SDK ships its own
  Berkeley DB 1.85 `db.h`, which `ovdb-private.h` rejects ("Need Berkeley
  DB 4.4 or higher").

- lib's 22 replacement functions (2026-10-30, `4c0c33482` through
  `4cb841856`): macOS and Linux have every function they replace, so
  neither build compiles them.  Each was checked with and without
  -DTESTING.  The 14 with tests are compiled into their tests with
  -DTESTING and run.  Changes: the TESTING prototypes got C linkage
  (BEGIN_DECLS/END_DECLS), four `void *` casts (asprintf, getaddrinfo,
  inet_ntop, snprintf), mkstemp's `template` parameter renamed `tmpl`, and
  strcasecmp's table written as octal integers instead of character
  literals ('\200' is negative where char is signed, a narrowing error).
  nullptr was applied with clang-tidy run directly (`a1e7a91c7`).

Apart from those lib tests, none of this links or runs the code; that waits for CMake-only goal item 8
(optional features).

### Testing gate for each directory

Before a directory is converted, every program CMake builds there is run
at least once by a test, and the directory reaches 33% line coverage (the
innd gate, applied per directory).  Programs that only need files run as
`.t` shell tests in verify-cxx.sh; those that need a server or stored
articles run in support/live-innd.sh.  Applied so far:

| Directory | Line coverage | Tests |
|---|---|---|
| nnrpd | 42.3% | live-innd.sh nnrpd section (`33d778dfa`) |
| backends | 41.5% | live-innd.sh backends section (`29f5c80e4`) |
| frontends | 50.4% | live-innd.sh frontends sections (`0380c3f5e`) |
| storage: tdx-util | 81.9% | tests/overview/tdx-util.t (`6106a013f`) |
| history: hissqlite-convert | 73.2% | lib/hissqlite-convert (`68cfe3743`, `d4d043362`) |
| storage: ovsqlite-server | 39.1% | overview/ovsqlite-integ.t (`9ef23a564`, `fb394e519`) |

tdx-util.t builds its overview from a tradspool spool and a history
written by makehistory.  Its inn.conf must use absolute paths: makehistory
changes to the history directory before reading the spool, so with
relative paths it finds no articles and writes an empty history without
an error.

nntpget and innxbatch always connect to port 119, so only their argument
and error paths run (15.4% and 15.9%); innbind (8.5%) binds ports for innd
and is covered only by its argument check.

### Modernization done so far

- **`nullptr`** in all compiled code (`9f32f9cc9` and following).  `NULL`
  remains in comments, in code compiled out in this build, and as variadic
  sentinels (`concat(..., NULL)`), which clang-tidy deliberately leaves.
- **`using` aliases** for the 11 single-line typedefs (`085d0741d`).  The 49
  struct/enum typedefs are left for class work, because the idiomatic C++ is
  `struct X { ... };`, not `using X = struct { ... };`.  clang-tidy 23.1.4's
  automatic fix corrupted anonymous struct typedefs, so it was not used.
- **`f()` instead of `f(void)`** in C++ definitions (`8aa1a361f`), and
  **`while (true)`** (`f9340d9ef`).
- **Formatting:** upstream INN is already `clang-format` clean, so converted
  files are kept that way (`7e5bee759`); a reformat never changes content.
- **On hold by decision:** C-style casts, uninitialized variables, implicit
  bool conversions (CLAUDE.md, "Deferred modernization").

### Security (decided 2026-10-29)

- No INN executable should run as root in production; they run as an
  unprivileged account such as `news` (tests run as the tester).  Upstream INN
  already drops to news and installs programs as news; the remaining root
  component is the setuid-root `innbind` port-binding helper.  Transient
  root that is dropped right away is acceptable (refined 2026-10-29), and
  innbind qualifies: it only runs for the news user, binds the allowed
  ports, hands back the sockets and exits.  The next task after the innd
  coverage gate is an audit confirming this holds everywhere, with tests.

### Security audit, first pass (2026-10-29)

Scope: every call that changes user or group IDs, every root check, the
install modes, the startup script, and which programs check their user at
all.  Method: `git grep -P` for `setuid`, `setgid`, `seteuid`, `setgroups`,
`initgroups`, `chroot` and for `getuid()`/`geteuid() == 0`; the `LI_*`
install macros in `Makefile.global.in` and `m4/modes.m4`;
`scripts/rc.news.in`; each program's `main` for a user check.

**What holds.**
- **innbind**, the only setuid-root program (`LI_SPRI`, mode 4550, owner
  root, group news): when privileged, it dies unless the real UID is the
  news user's, which it takes from the compiled-in `RUNASUSER` (it never
  reads inn.conf, so `INNCONF` in the environment cannot change it).  It
  binds only ports 1024 and up, or 119, 433, 563 and the configured innd
  port (innbind.cc:182), passes each socket back and exits.
- **innd** drops to news as its first real step (`ensure_news_user_grp`,
  innd.cc:408; only `openlog` runs before it) and binds its ports through
  innbind afterwards.
- **nnrpd -D** binds its sockets, as root if started as root, then drops to
  news before serving anyone (nnrpd.cc:1180).
- **rc.news** refuses to run as anyone but the news user.
- **expire, expireover, makedbz, makehistory, sm, tdx-util** drop to news
  if run as root; the **ovdb** tools and ovdb_server refuse to open the
  database unless running as the news user (`ovdb_check_user`).
- **Optional install modes** never involve root: `--enable-setgid-inews`
  installs inews setgid news (02555), `--enable-uucp-rnews` installs rnews
  setuid news, group uucp (04550).  The CMake install rules (goal item 5)
  must offer the same choices with the same defaults (inews 0550, rnews
  0500).

**Findings.**
1. **Supplementary groups are kept when dropping root.**
   `ensure_news_grp`/`ensure_news_user` (lib/newsuser.cc) call `setgid`
   then `setuid`, in the right order, but never `setgroups`, so a process
   started as root keeps root's supplementary groups (on macOS typically
   `wheel`, `kmem`, `tty`, `admin` and others) after the drop: an
   incomplete privilege drop (CWE-271).  Affects every program in "What
   holds" that drops from root.  `configure.ac` already checks for
   `setgroups`; nothing calls it.  **Fixed** (2026-10-29): `ensure_news_grp`
   calls `setgroups(1, &gid)` before `setgid` when root, failing hard on
   error.  Tested as root by support/root-checks.sh with
   tests/lib/newsuser-t.c: a file readable only through a group root has
   and the news user lacks is still readable after a drop without
   `setgroups` (so the leak is real on macOS) and refused after
   `ensure_news_user_grp`.  The test checks access, not `getgroups()`:
   with macOS's default compile settings `getgroups()` returns the user's
   directory group list, not the list set by `setgroups()` (getgroups(2)).
   Apple's setgroups(2) says only that its use is "highly discouraged";
   initgroups(3) explains that the kernel treats the credential's group
   list as an initial cache and resolves other memberships through
   opendirectoryd, and that membership checks should use
   `mbr_check_membership(3)`.  Neither says a privilege drop should keep
   root's groups, and the access test shows `setgroups` removes them.
2. **nnrpd started as root without -D keeps root.**  The drop is inside
   the daemon-mode block only.  Run from inetd/xinetd as root (the docs say
   to use the news user, but nothing enforces it), nnrpd serves the whole
   reader session as root.  **Fixed** (2026-10-29): the non-daemon branch
   calls `ensure_news_user_grp` when the effective UID is root, before
   `SetupDaemon()` opens storage and before any client I/O; only
   `innconf_read` runs earlier.
3. **rnews started as root drops only its UID.**  It calls `setuid(news)`
   but not `setgid` or `setgroups` (rnews.cc:870), so it keeps group 0 and
   root's supplementary groups while it processes batches from the network.
   It avoids `ensure_news_user` on purpose, to allow rnews installed setuid
   news; the group drop can still be done when the real UID is root.
   **Fixed** (2026-10-29): when the effective UID is root, rnews calls
   `ensure_news_grp(true)` (group and supplementary groups) before its
   `setuid`.  Before the fix, the root check saw its spooled batch owned
   by the news UID but group 0.
4. **innfeed and ovsqlite-server never check their user.**  Both are
   long-running servers normally started by innd or rc.news as news; run
   directly as root, each keeps root for its lifetime.  **Fixed**
   (2026-10-29): both call `ensure_news_user_grp` right after
   `innconf_read`, before opening anything.  `inn/newsuser.h` now includes
   `<sys/types.h>` itself, which ovsqlite-server.c needed.
5. **Most other programs never check their user** (ctlinnd, inews, innxmit,
   batcher, the authenticators, ...).  Run as root by an administrator they
   act as root, and may leave root-owned files in news-owned directories.
   That is a correctness and hygiene issue more than an escalation (root is
   already root); a candidate policy is that programs which write INN's
   data refuse root, as rc.news does.  **Decided and done** (2026-10-29):
   `ensure_not_root()` (lib/newsuser) dies when the effective UID is 0, and
   the programs that change INN's spool, history, overview, batch files or
   logs call it first thing: innxmit, batcher, buffchan, overchan, archive,
   fastrm, prunehistory, shrinkfile, innxbatch.  Exempt: read-only tools
   (ctlinnd, grephistory, sm without -d, getlist, inndf, convdate,
   innconfval, cvtbatch, actsync, gencancel), inews (posts through the
   server), shlock (a general locking utility for scripts), ninpaths
   (writes only the file it is given; links no INN library).  Programs
   that already drop from root (expire, makehistory, ...) keep doing so.

**Scripts (second pass, 2026-10-29).**  Of the 61 `.in` scripts, the
ones that write INN's data now refuse root, with the check written out
right after the line fixscript replaces (so `${NEWSUSER}` or
`$INN::Config::runasuser` is set): shell `[ "$(id -u)" = 0 ]`, Perl
`$> == 0` inside a `BEGIN` block, so it runs before the script's other
`use` lines are compiled and a missing module cannot pre-empt it.
- Shell: news.daily, scanlogs, innwatch, writelog, tally.control,
  nntpsend, send-ihave, sendxbatches, controlbatch, actsyncd, expirerm.
- Perl: send-uucp, controlchan, perl-nocem, procbatch, innreport,
  hissqlite-util, ovsqlite-util, cnfsheadconf.
- Already checked: rc.news (refuses anyone but news), mod-active (dies
  unless running as news).
- Exempt: read-only or reporting tools (innstat, cnfsstat, scanspool,
  docheckgroups and pgpverify, whose results controlchan acts on,
  actmerge); clients and mail (mailpost, pullnews, news2mail, sendinpaths,
  innmail, simpleftp, delayer); inncheck (its -f mode fixes ownership
  with chown, which needs root); innupgrade (run as root by `make update`
  at install time, and it chowns what it writes to news); build-time
  helpers (fixscript, sqlite-helper-gen).
- No script calls su, sudo or a setuid function, and none is installed
  setuid or setgid.

**contrib/ (reviewed, not changed).**  Unsupported, built only on request
and not installed (contrib/README).  No C program or script in it changes
user IDs.  Recommendations: `auth_pass` checks `pw_passwd` from
`getpwnam`, which on shadow-password systems is only readable by root, so
it works only as root; use ckpasswd (shadow and PAM support) instead.
`mlockfile` needs root or a raised `RLIMIT_MEMLOCK` for mlock; raise the
limit for the news user rather than running it as root.  The scripts that
modify data (fixhist, thdexpire, ovsqlite-undump, ...) should be run as
news.

**fastrm.**  Its `unlink_dangerous` branch (as root, stat each name and
refuse directories, since root may unlink them on some systems) could no
longer run once fastrm refused root; removed, and fastrm(8) now says it
refuses root.

**Not auditable yet.**  The CMake install rules (goal item 5) do not exist;
when written they must keep innbind the only setuid-root file and offer
the setgid-inews and uucp-rnews choices (see "What holds").

**Test builds.**  build-live and build-cov compile in the tester's account
as `RUNASUSER` (INN_RUNAS_USER), so an innbind from those builds would
accept the tester; they must never be installed.

**Testing the fixes** (decided 2026-10-29: a separate root run).  The
policy runs tests as the tester, so the started-as-root paths cannot run
in the normal checks.  `support/root-checks.sh` runs them as root against
a build whose news user is the tester (build-live on macOS, run by hand
with sudo; build-linux in Docker on Linux via
support/docker-root-checks.sh, as the container's root).  It uses a
scratch installation owned by the news user and checks ownership of what
each program creates rather than polling processes, except for nnrpd,
whose UID is read while a client holds its session open.  Every new check
was also run against the code without the fixes, and failed there.

### Running innd as the tester for live tests (decided 2026-10-29)

**What the code does.**  innd calls `ensure_news_user_grp()` (innd.cc, near
line 408) **before** it reads inn.conf (`innconf_read()`, line 433).  At that
point `innconf` is still null, so `ensure_news_user()` and `ensure_news_grp()`
(lib/newsuser.cc) fall back to the compiled-in `RUNASUSER` and `RUNASGROUP`
from `config.h` (`"news"`), which configure sets from `--with-news-user` and
`--with-news-group`.  So the `runasuser`/`runasgroup` keys in inn.conf cannot
make innd run as another account: the account is fixed when innd is built.
(An earlier statement in this conversion that inn.conf controlled it was
wrong; the live test showed `can't resolve news to a GID`.)  The ordering is
deliberate: if innd is started as root it drops privileges before reading
any configuration file.

**Options considered.**
1. A CMake option for test builds only (chosen).
2. Rerun `./configure --with-news-user=<tester>`: changes `config.h` for every
   build, including production.
3. Make innd read inn.conf before checking the user: a behavior change to
   upstream code made only for testing, and it would read configuration files
   as root before dropping privileges, against the security policy above.

**What was done.**  CMake now always writes its own copy of configure's
`config.h` to `<build>/include/config.h`, first on the include path, and
replaces `RUNASUSER`/`RUNASGROUP` there when `-DINN_RUNAS_USER` /
`-DINN_RUNAS_GROUP` are set.  With the options unset the copy is
byte-identical to configure's, so production builds are unchanged.  The code
under test is the same; only the compiled-in account differs, as configure
already allows.  Two details made this safe:
- `config.h` has no include guard, so two copies in one compilation would
  silently redefine each other.  All 200 compile commands were checked to put
  `<build>/include` before `include/`.
- 79 of the 80 files including `"config.h"` resolve it through the include
  path; `include/innperl.h`, being next to the original, would have found it
  first.  It now uses `#include <config.h>` (inside `#if DO_PERL`).

**Live test.**  `support/live-innd.sh <build>` builds a scratch installation
(inn.conf with every path in a temporary directory, 127.0.0.1 on an
unprivileged port, so neither root nor innbind is involved), starts the real
innd as the tester, feeds five articles with rnews, checks history (grephistory),
the spool (sm), a file feed and duplicate refusal, then exercises 28 of
ctlinnd's 37 commands with a check of each one's effect (state: throttle,
pause, reserve, go; articles: cancel, addhist; groups: newgroup, rmgroup,
changegroup, renumber, lowmark; feeds: checkfile, name, feedinfo, flush,
flushlogs, drop, begin, send to an exploder feed, reload, param; status:
status, stathist, timer; connections: reject, allow, hangup, readers,
logmode), and shuts down.  Not exercised: perl and python (off in this
build), signal, trace, xabort and xexec.  innd runs with -f, logging to
files as in production.  Use a build configured with
`-DINN_RUNAS_USER=$(id -un) -DINN_RUNAS_GROUP=$(id -gn)` (e.g. build-live).

**Result.**  innd line coverage 9.5% -> 33.9% (art.cc 45%, chan.cc 56%,
newsfeeds 35%, rc 26%, cc 25%, site 21%, nc 20%), passing the 33% gate;
overall 47.5%.

**Found a test gap?**  If you notice innd behavior that none of these tests
exercises (a command, a feed type, an error path), please report it as a bug
in the project's issue tracker
(<https://github.com/ecarew/inn2/issues>), saying what is untested and, if
you can, how to reproduce it, so it can be reviewed and worked into the
tests.  `support/coverage-report.sh build-cov --html` shows line by line what
the tests reach.

### Not converted

- **INN's test programs stay C** (decided 2026-10-29).  The 76 test programs
  in `tests/` are compiled as C and linked against the converted C++
  libraries and innd objects.  That is a deliberate, continuous check of the
  C/C++ boundary: if a header lost its C linkage or a function's exported
  name changed, the C tests would fail to compile or link, which is exactly
  the breakage an existing C user of libinn, or the Perl/Python embedding,
  would hit.  Keeping them unchanged also keeps the differential testing
  honest: the same test source runs against the original C build and the
  C++ build.  New tests written for the conversion (`tests/storage/methods-t.c`)
  follow the same rule.
- **Perl and Python stay** (2026-10-28).  The Python in `samples/` and the
  embedded Perl/Python filter and authentication hooks are INN's extension
  interface for sites; the ~25,600 lines of Perl are administration tools that
  Perl does well; `INN::Config` is part of INN's Perl interface.

## Class design approach (agreed 2026-10-28)

**Goal:** a class's interface comes from what callers do with the data
("look up this header", "store this article and return a token"), not
from its list of fields.  Doxygen's "referenced by" lists and call graphs
are the tool for finding those operations, used after the architecture is
mapped, not as the starting point.  No blanket getters and setters.

**INN is already object-oriented, in C.**  Much of the design is finding the
objects that are there:

| C pattern in INN | Where | C++ form |
|---|---|---|
| Tables of function pointers, one per backend | `STORAGE_METHOD` (cnfs, timecaf, timehash, tradspool, trash), `HIS_METHOD` (hisv6, hissqlite), `OV_METHOD` (tradindexed, buffindexed, ovsqlite, ovdb) | abstract base classes, one derived class per backend |
| Opaque handles with open/close | `struct history *` (`HISopen`/`HISclose`), `struct overview *`, `SMinit`/`SMshutdown` | RAII classes |
| Struct plus functions sharing a prefix | `ART*`, `NG*`, `SITE*`, `CHAN*` | one class per prefix |
| Callbacks stored in a struct | `CHANNEL` reader/write-done handlers in innd's event loop | virtual functions or `std::function` |
| Global state | `innd.h` externs, `innconf` | a context or configuration object |

**Three layers, bottom-up:**
1. Interfaces at the seams: storage, history and overview.  Clearest objects,
   best tested, shared by many programs.  First round.
2. innd's domain objects: articles (`ARTDATA`, `HDRCONTENT`, headers),
   channels, sites, newsgroups.  Central, but currently almost untested, so
   **innd needs at least 33% line coverage before any further C++
   conversion work anywhere in the tree** (decided 2026-10-28), so that no
   change lands without tests that would catch lost compatibility with
   existing INN installations.
3. Utilities (`lib/vector`, `hashtab`, `buffer`, `list`): replaced by standard
   containers as the layers above stop needing them.

**C compatibility is not a goal in itself.**  It is kept only where something
outside INN demonstrably has to compile or link against the code: the Perl
and Python embedding (callbacks and extension functions need C linkage), and
any external program linking libinn.  Everything else may become C++-only once
its last in-tree C user is converted.

**Generated code too:** innfeed's configuration parser should move to the C++
outputs of bison (`lalr1.cc` skeleton) and flex (C++ scanner) as part of
converting innfeed.

## Planned design (not yet implemented)

This section is a starting point only.  Further C++-specific design and
refactoring (classes, memory management, containers) will get its own
document once the conversion is complete and working (decided 2026-10-29);
this file stays the record of the conversion itself.

### Article header lookup: sorted array instead of a tree (decided 2026-10-28, done 2026-10-30)

`innd/art.cc` finds each header line's entry among the 76 known headers
(`MAX_ARTHEADER`).  Today `ARTbuildtree()` builds a balanced binary search
tree (`TREE`) at startup, by repeatedly taking the middle of the sorted
`ARTheaders[]` table, and the parser searches it case-insensitively
(`strcasecmp`) for every header line of every article.  The tree is never
modified after startup.

The plan is a `HeaderTable` class holding a sorted array, searched with a
case-insensitive `std::lower_bound`.

| | Balanced BST (today) | Sorted array (chosen) | `std::unordered_map` | Perfect hash |
|---|---|---|---|---|
| Lookup | O(log n): at most 7 comparisons for n = 76 | O(log n): the same 7 | O(1) average, O(n) worst | O(1) worst case |
| Build | O(n), 76 `malloc`s at startup | none if built at compile time (`constexpr`) | O(n) with allocations | none (generated) |
| Memory | 76 heap nodes × 4 pointers | 76 contiguous entries | most overhead | small table |
| Cache | up to 7 pointer hops across the heap | contiguous, a few cache lines | full-name hash, then 1–2 accesses | hash, then 1 access |

Why the sorted array:

- **Same complexity as today**, same number of comparisons, so no
  algorithmic regression, and the cheap first-letter comparison carries over.
- **Faster in constant factors** (contiguous memory instead of pointer
  chasing), though the difference is nanoseconds per lookup: the lookup runs
  10–30 times per article and is small next to `innd`'s disk and network I/O.
- **Simpler and safer**: no allocation, no `ARTfreetree()`, nothing to leak,
  immutable after construction.
- **Rejected:** a hash table must case-fold and read the whole name to hash it,
  so at n = 76 it is no faster and brings back allocation; a perfect hash adds a
  generator for no measurable gain.

That was analysis, not measurement.  **Measured 2026-10-30**
(`support/bench/header-lookup.sh`: the 76 names from innd.cc, the header
lines of tests/data/articles plus support/bench/header-mix.txt; ns per
lookup, median of 15 rounds, hot cache):

| | macOS arm64, Apple clang 21 | Debian aarch64, GCC 14 |
|---|---|---|
| Tree (before) | 21.1 | 14.2 |
| Sorted array, `std::lower_bound` (chosen) | 37.4 (1.75×) | 20.8 (1.46×) |
| Sorted array, the tree's own search | 23.3 (1.10×) | 15.3 (1.08×) |
| Eytzinger (breadth-first) array | 20.0 (0.95×) | 13.7 (0.96×) |
| `std::map` | 60.6 (2.8×) | 24.3 (1.7×) |

So the claims above were wrong in part: the whole table fits in L1, so
the array gains nothing from being contiguous, and `std::lower_bound` is
slower because it cannot stop at a match (it runs every step, then compares
once more).  Only the Eytzinger layout beats the tree, by about 5%.  The
first benchmark run reported 0.5 ns for the tree: the compiler had hoisted
the repeated lookups out of the loop.  The bench now hides each input and
result from the optimizer.

**Chosen (by the user): `std::lower_bound`**, for well-traveled standard
library code over a hand-written search.  The cost is about 15 ns per header
line, under half a microsecond per article, next to innd's history, disk and
network work per article.  On the plus side, it retires the lookup's
manual memory management: 76 `xmalloc`s for tree nodes at startup, the
temporary `xmalloc`ed table sorted with `qsort` and then `free`d, and the
recursive `ARTfreetree` that `ARTclose` had to call, replaced by a fixed
`std::array` with no allocation and nothing to free (art.cc: 65 code lines
removed, 40 added).  Done in innd/art.cc: a file-local `HeaderTable`
(a `std::array` of pointers into `ARTheaders`, sorted once by ARTsetup, no
allocation), reached through `ARTfindheader()` in innd.h, which
tests/innd/headers-t.c tests (every name in four cases, 14 non-names).
`TREE`, `ARTbuildtree`, `ARTcompare` and `ARTfreetree` are gone.  The
first-letter shortcut the tree used was dropped with it.

### innd's in-memory article: ARTDATA and HDRCONTENT in C++ (plan, 2026-10-30)

Step 1 of the plan the user approved: who touches what, and what is
tested, before any field changes.  No code has changed yet.

**Today.**  innd never allocates per article: each `CHANNEL` embeds an
`ARTDATA` (innd/innd.h) that is reused for every article on the channel,
and the article is parsed in place in the channel's input buffer.
`HDRCONTENT` (one per known header field, `HdrContent[MAX_ARTHEADER]`)
points into that buffer with a length; `LastChar` keeps the byte the
parser overwrites with `'\0'` (`HDR_PARSE_START`/`HDR_PARSE_END`) so a value
can be used as a C string.  `ARTDATA` has 36 fields: offsets, times,
counters, fixed `char[SMBUF]` buffers (LinesBuffer, Bytes), heap strings
with separate length/capacity fields (Xref/XrefLength/XrefBufLength,
Replic/ReplicLength), three `LISTBUFFER`s (Newsgroups, Distribution, Path:
a data buffer plus a pointer list, each with a length) and two libinn
`struct buffer`s (Headers, Overview).

**Who touches it** (accesses through an `ARTDATA`, 2026-10-30): about 300,
some 80% in art.cc; the rest in chan.cc (the `LISTBUFFER`s, Xref, Overview:
it frees them), site.cc (what feeds write: times, Bytes, TokenText,
Replic, Feedsite, the flags), cc.cc (times; it also builds two `ARTDATA`s
on the stack, for ctlinnd cancel and addhist) and nc.cc (Body, CurHeader,
BytesHeader).  perl.cc and python.cc read header values only through the
`HDR()` macros (5 uses); keywords.cc reads `HDRCONTENT` directly (9).

**Constraints found.**
1. innd.h is compiled as C by tests/innd/artparse-t.c, chan-t.c and
   headers-t.c (tests stay C).  They read only `Data.Body` of the article,
   but they build whole `CHANNEL`s, by copying a zeroed C struct.  A C++
   member (a `std::string`) in `ARTDATA` would stop them compiling.
2. innd itself does the same: chan.cc fills its `xmalloc`ed channel table
   with `*cp = CHANnull`, valid only for a plain C struct.
3. Cleanup is by hand: chan.cc frees each `LISTBUFFER`'s pieces and the
   Xref buffer; art.cc resets fields per article (ARTprepare).

**Coverage of the paths involved** (build-cov, verify-cxx.sh plus
live-innd.sh, region coverage, 2026-10-30): art.cc 43.5% overall.  Well
covered: ARTparse, ARTprepare, ARTcheckheader 84%, ARTparseheader 89%,
ARTcontrol 94%.  Partly: ARTassignnumbers 63%, ARTparsepath 62%,
ARTmakeoverview 57%, ARTstore 47%, ARTclean 47%, ARTpropagate 42%,
ARTpost 31%.  Not at all: ARTxrefslave (the Xref of xrefslave mode),
ARTparsedist 8% and DISTwanted/DISTwantany (the Distribution list),
ARTpoisongroup, ARTsendthegroup, the hashfeed functions.  So the Xref and
Distribution fields, two of the first candidates, are the least tested.

**Design.**  Keep `ARTDATA` a C struct for the C code and tests, and move
the C++ state behind it: an opaque `struct ArticleState *` member (C sees an
incomplete type) to a C++ class owned by the channel, created and destroyed
in chan.cc (CHANcreate/CHANclose) instead of being copied in with
`CHANnull`.  Fields move into the class one group at a time; C code and
the tests reach them through small `extern "C"` accessors where needed.
Per-article reset becomes one `clear()`, and frees go away.

**Steps** (each its own verified commits; each measured with the live test,
and with a parse benchmark like support/bench/header-lookup if it touches
the per-article loop):
1. Done: this analysis.
2. Done 2026-10-30: tests first, in support/live-innd.sh: site and ME
   distributions (including `!` negation), a poisoned nonexistent group, a
   newgroup for a group that does not exist yet, MD5 and quickhash
   hashfeeds, xrefslave mode (innd restarted with `xrefslave: true`: filed
   under the Xref numbers, malformed entries skipped, missing or useless
   Xref rejected), and ARTclean's rejections (missing required field, bad
   Message-ID, Lines with linecountfuzz, bad Date and Injection-Date, too
   old, future, a colon in Newsgroups; Expires accepted, valid or not).
   Region coverage: art.cc lines 48.9% to 65.9%; ARTxrefslave 0 to 84%,
   ARTparsedist 8 to 72%, DISTwanted 0 to 100%, ARTpoisongroup and
   ARTsendthegroup 0 to 100%, hashfeed 0 to 83-94%, ARTclean 47 to 87%,
   ARTpost 31 to 45%.  Each untested path a step below touches still gets
   a test before the step.

   Learned on the way: in newsfeeds, distributions go in the pattern field
   (`site:*/local:...`); `site/x` lists exclusions.  ME's subscription is
   put in front of every site's, so a site that wants only some groups
   starts with `!*`.  rnews checks Subject and Message-ID itself, so those
   rejections are tested over IHAVE.  In xrefslave mode tradspool takes
   the article's path from the first Xref entry and refuses the article if
   that entry is malformed, though ARTxrefslave skips such entries (upstream
   behavior, left as is).  HashFeedMatch's `else continue;` does not
   advance to the next entry, so an unknown hash type would loop for ever;
   unreachable today (the parser makes only the two known types), listed as
   a refactoring candidate.
3. Add `ArticleState`, empty, with its creation and destruction in chan.cc
   and cc.cc's stack uses.
4. Generated strings: Xref (and its length/capacity), Replic, LinesBuffer,
   Bytes to `std::string` in `ArticleState`.
5. The `LISTBUFFER`s (Newsgroups, Distribution, Path) to
   `std::vector<std::string_view>` over one owned buffer; then Headers and
   Overview from `struct buffer` to `std::string`.
6. `HDRCONTENT` last: values as `std::string_view` into the input buffer,
   so the in-place `'\0'` and `LastChar` restore go; C users (perl.cc,
   python.cc, the tests) get NUL-terminated copies or accessors.  It is on
   the hot path, so benchmark it.

**Boost** (required, see "Boost in the design"): Boost.Container's `static_vector` (fixed capacity, no heap)
and `small_vector` (inline first, then heap) fit steps 4 and 5 better
than `std::vector` where innd today uses fixed buffers.

### Boost in the design (decided 2026-10-30: required)

The user wants Boost formally in the design, for breadth beyond the standard
library and for its review history; Boost libraries also tend to move into
the standard later (Boost.Filesystem, Optional, Variant and Asio's
networking work all did).  Proposed order of preference: the standard
library, then Boost, then INN's own code.

Most useful here (header-only unless noted):
- Boost.Container: `static_vector` (fixed capacity, no heap: innd's
  `char[SMBUF]` and `MAX_ARTHEADER` arrays), `small_vector` (newsgroup and
  path lists, usually short), `flat_map`/`flat_set` (sorted-vector maps;
  `std::flat_map` is C++23).
- Boost.Intrusive: lists and trees whose nodes live inside the objects
  (no allocation per element), for INN's pointer-linked structures: the
  channel and site lists, the history cache, overview indexes.  Its trees
  (red-black, AVL, splay, scapegoat) are what to compare INN's own trees
  with.
- Boost.Algorithm: `iequals`/`ilexicographical_compare` and string helpers,
  in place of hand-written case-insensitive code.
- Boost.Iostreams and Boost.Asio: see "Positioned file I/O" (pread/pwrite).

**Decided (the user, 2026-10-30): Boost is a required dependency.**  The
user has used it for years on several platforms; an optional dependency
would mean keeping two code paths wherever it is used.  Version 1.92 or
later (the user's choice; Homebrew has 1.92, Debian trixie's libboost-dev
only 1.83).  Header-only
libraries only, at first, so nothing is linked: the root CMakeLists.txt
requires it with `find_package(Boost 1.92 REQUIRED CONFIG)` (CMake 4 has
no FindBoost module) and code that uses it links `Boost::headers`.  A
compiled Boost library needs its own decision.

### On-disk formats: typed access, Boost.Endian, no Boost.Serialization (decided 2026-10-30)

INN's files keep their formats exactly: history (dbz and the text
history), overview (tradindexed and buffindexed indexes, ovsqlite), CNFS
cycbuff headers, storage tokens, the active file.  Existing sites upgrade
in place, and INN's Perl modules, contrib scripts, admins' tools and other
INN versions read them.  The C++ work changes how INN reads and writes
them, not what is on disk.  Agreed with the user:

- **Typed readers and writers per format.**  Each binary record is a
  struct with an explicit layout, its size and field offsets pinned by
  `static_assert` against the format, read and written through
  `std::bit_cast` or `std::span` over the bytes (often an `mmap`ed
  region) instead of casts of raw pointers.  The format is unchanged; the
  access is checked at compile time.
- **Boost.Endian** (header-only) where a format's byte order matters: its
  types carry their byte order (`big_uint32_t` and so on), so fields are
  right on any CPU without hand-written swapping.  Formats that today use
  the host's order keep it, unless a change is agreed (and then it is a
  format change, with a migration).
- **Boost.Serialization only for new state that only INN++ reads**, if any
  appears; none does today.  Not for INN's existing formats: it writes only
  its own archive format, reads whole archives as streams (INN reads its
  files in place at random offsets), and it is a compiled library, which
  needs its own decision (see "Boost in the design").

Each format gets tests that read files written by the original C code
(tests/data) and compare bytes written by the new code with the
original's, before its access code changes.

### Memory management: from C to standard C++ (goal, 2026-10-29)

**Goal.**  Move C-style memory management to standard C++ practice wherever
possible: RAII ownership, `std::string` and `std::string_view`, `std::vector`
and the other standard containers, and `std::unique_ptr` (with custom
deleters for C resources such as `FILE *`, file descriptors and `mmap`
regions).  If this works completely, the memory-management compatibility code
goes away: the `xmalloc` family and `inn_void_ptr` (`include/inn/xmalloc.h`,
`lib/xmalloc.cc`), `concat`/`concatpath`, the `reallocarray` fallback, and
INN's C containers (`lib/buffer`, `lib/vector`, `lib/hashtab`, `lib/list`)
with their headers.

**Scale** (2026-10-29, `.cc` files): about 650 allocations through
`xmalloc`, `xcalloc`, `xrealloc`, `xreallocarray`, `xstrdup`, `xstrndup`,
`xasprintf`, `concat` and `concatpath`; 13 direct `malloc`/`calloc`/`realloc`;
934 `free()` calls; plus `buffer_new` (6), `vector_new` (16), `cvector_new` (5)
and `hash_create` (6).  Track these counts as the work proceeds.

**Constraints that shape it.**
- **External C consumers** (the Perl and Python embedding, external users of
  libinn) may still need a C API; keep thin C entry points there rather than
  the C internals.
- **Out-of-memory behavior.**  `xmalloc` never returns null: on failure it
  calls `xmalloc_error_handler`, which logs and exits.  `operator new` throws
  `std::bad_alloc` instead, and INN has no exception handling.  Decide the
  policy before converting (e.g. a `std::set_new_handler` that keeps today's
  log-and-exit, or catching at the boundaries); exceptions must never cross
  into C code or callbacks.
- **Ownership crosses function boundaries.**  Many functions return memory
  the caller must `free()`; convert callee and callers together, layer by
  layer, following the three-layer order above.
- **Mapped and shared memory stays a view.**  Spools and overview files that
  are `mmap`ed or in System V shared memory (cnfs, buffindexed, tradindexed,
  timecaf) are not heap objects; wrap the mapping in RAII and use
  non-owning views (`std::span`, `std::string_view`) into it.
- **Hot paths.**  Article parsing and feeding are performance-sensitive;
  prefer `std::string_view` and reusable buffers over per-call `std::string`
  copies, and measure where it matters.
- **Tests first**, as for all class work: no conversion of code without
  coverage.

### Other first classes (proposed)

- `enum class ArtHeaderType` from `ARTHEADERTYPE`.
- `ArtHeader`: immutable value type (`std::string_view` name, type) from
  `ARTHEADER`; `Size` is `name.size()`.  The table becomes a `constexpr`
  array.  `std::string_view` rather than the WIP branch's `std::string`,
  because the names are compile-time constants.
- `OverviewSchema` from `ARTfields` (`ARTOVERFIELD`), built in
  `ARTreadschema()` as a `std::vector`, replacing the `xmalloc`/`free` pair.

These start with structs private to `art.cc`.  Structs in headers still
included by C code stay C-compatible until their last C user is converted.
Before any of this, INN's `innd/artparse` test must join the verification
checks: `art.cc` currently has no test coverage.

### Retiring ovdb and Berkeley DB (proposed 2026-10-29, refactoring phase)

**Proposal.**  After the C++ and CMake conversion, retire the ovdb overview
method and with it INN's dependency on Berkeley DB.  Condition set by the
user: a **one-way converter** from existing ovdb databases to ovsqlite, and
the converter itself must **not depend on Berkeley DB**.

**Why.**
- Oracle relicensed Berkeley DB 6.0 under AGPLv3 in 2013
  ([LWN](https://lwn.net/Articles/557487),
  [debian-devel](https://lists.debian.org/debian-devel/2013/07/msg00173.html)).
  Fedora stayed on the last LGPL release, 5.3.28, and deprecated `libdb` in
  Fedora 33 as the first step toward removing it
  ([Fedora change page](https://fedoraproject.org/wiki/Changes/Libdb_deprecated),
  [tracker](https://bugzilla.redhat.com/show_bug.cgi?id=1361971)).
  Other projects have moved off it: 389 Directory Server to LMDB, bogofilter
  to SQLite
  ([Fedora devel-announce, 2023-11](https://lists.fedorahosted.org/archives/list/devel-announce@lists.fedoraproject.org/2023/11/?page=1)).
  Not yet confirmed: whether Fedora has since removed `libdb`, and Debian's
  current status.
- INN does not deprecate ovdb (doc/pod/ovdb.pod,
  [INN docs](https://www.eyrie.org/~eagle/software/inn/docs/ovdb.html)), but
  its own news notes say ovdb "has not been as widely tested as the other
  overview methods", has known locking and timing issues with nnrpd, and needs
  a database upgrade whenever the Berkeley DB library changes.
- ovsqlite covers the same role with SQLite, which INN already links.
- `storage/ovdb/ovdb.cc` fails the C++ syntax check with Berkeley DB
  enabled (CLAUDE.md, "Other known gaps"); retiring it avoids that work.

**Scope.**  About 5,900 lines: `storage/ovdb/`, `frontends/ovdb_init`,
`ovdb_monitor`, `ovdb_server`, `ovdb_stat`, `m4/bdb.m4` (already moot once
the build is CMake-only), and the documentation.  Separate decision:
`authprogs/ckpasswd` can read dbm password files through Berkeley DB's ndbm
layer (`HAVE_BDB_NDBM`); without Berkeley DB it would use the system `dbm`
only.

**The converter without Berkeley DB.**  Reading Berkeley DB files without
its library may or may not be practical; to be settled by a prototype
before ovdb is removed.  Things to establish:
- **On-disk format.**  Which access methods and page layouts ovdb's
  databases use (btree is expected; check overflow pages, byte order and
  the format versions in the field), and whether those formats are
  documented well enough to read independently.  The record layout on top
  (ovdb's keys, data and its zlib compression, ovdb format versions 1-3) is
  INN's own and is in `storage/ovdb/`.
- **Consistency.**  ovdb runs in a transactional environment, so recent
  changes can sit in the log files rather than the database pages.  A
  converter that reads only the pages needs the site to stop INN and
  checkpoint cleanly first, or it may miss or misread records.  Decide how
  the converter detects an unclean database and refuses.
- **Fallbacks if direct reading is impractical:**
  1. Read the portable text output of Berkeley DB's own `db_dump` utility.
     The site runs `db_dump` with the Berkeley DB it already has installed;
     INN's converter parses only text and has no Berkeley DB dependency.
  2. Rebuild the overview from the spool with `makehistory -O` after
     switching `ovmethod` to ovsqlite.  It needs no converter, but on a
     large spool it can take a long time.
- **Tests.**  Fixture databases written by real Berkeley DB versions (4.4
  through 18), converted and compared with ovsqlite's contents through the
  overview API (`tests/overview/api` style).

**Plan.**  (1) Mark ovdb deprecated in the documentation and the CMake
option, pointing to ovsqlite.  (2) Ship and test the converter.
(3) Remove ovdb in a later release.  Because this changes what INN
supports, raise it with the upstream INN maintainers first; they may
already have a position the documentation does not show.

### Positioned file I/O: pread/pwrite in modern C++ (noted 2026-10-30, refactoring phase)

**What INN uses them for.**  `pread` and `pwrite` read or write at a given
file offset in one call and leave the descriptor's file position alone,
so code sharing a descriptor does not race on `lseek`.  Callers: the
storage methods (`storage/cnfs/cnfs.cc`, `storage/buffindexed/buffindexed.cc`,
`storage/tombstone.cc`), history (`history/hisv6/hisv6.cc`, `lib/dbz.cc`),
`expire/expireover.cc`, and `lib/xwrite.cc` (`xpwrite`).  `lib/pread.cc` and
`lib/pwrite.cc` are fallbacks for systems without them; macOS and Linux
both have them, so neither build compiles the fallbacks.

**Standard C++ has no equivalent** (as of C++23).  Candidates looked at:
- `std::spanstream` (C++23) is a stream over a buffer in memory.  It does
  no file I/O, so it cannot replace either call.
- Boost.Iostreams wraps files as streams, but a positioned read is a seek
  then a read: two calls, and it moves the shared file position, which is
  exactly what `pread` avoids.
- **Boost.Asio's `random_access_file`** (Boost 1.78 and later) has
  `read_some_at` and `write_some_at`, which match `pread`/`pwrite`, plus
  asynchronous forms.  This is the only real match found.  Costs: a Boost
  dependency INN does not have today, and rewriting the callers.

**Why Boost is worth considering anyway.**  Boost libraries have often
moved into the standard after a few years: smart pointers, `function`,
`bind`, regex, thread, chrono and random in C++11; `optional`, `variant`,
`any`, `string_view` and `filesystem` in C++17.  Asio was the basis of the
Networking TS (ISO/IEC TS 19216:2018), though that TS has not been merged
into the standard; C++26 took the senders/receivers model (`std::execution`)
instead.  To check, with citations (cppreference / WG21 papers), before
relying on it: whether any C++26 or later proposal adds positioned file
I/O.

**Options for the refactoring phase.**
1. Keep `pread`/`pwrite` behind a small C++ wrapper of INN's own (for
   example a file class with `read_at`/`write_at` returning a span or an
   expected-style result), and drop the C fallbacks, since every target
   platform has the calls.  No new dependency; the wrapper's interface can
   follow Asio's so a later switch is mechanical.
2. Adopt Boost.Asio's `random_access_file` directly, if INN takes on
   Boost for other reasons too (networking in innd/nnrpd/innfeed would be
   the bigger reason).
3. Switch to a standard facility if one is adopted.
Option 1 is the cheapest first step and does not rule out 2 or 3.

### Lesson from the audit regression (2026-10-30)

`ensure_news_user_grp()` is not a "drop root if root" call: when the
process is not root it dies unless it already is the news user and group.
Code that only means to drop root must guard it with `geteuid() == 0`.
`af8441c0f` did not, for ovsqlite-server and innfeed, and the live harness
could not notice because build-live makes the tester the news user.  The
fix is `4780a0b0c` and `a885baf6d`.  When adding a privilege drop, also
run the program as a non-root user who is not the news user (verify-cxx.sh's
build/ is configured with the default news user, so its tests do this).

## Conversion log

History moved here from CLAUDE.md on 2026-10-30, verbatim, so CLAUDE.md
holds only the rules and the open items.  New resolved items and findings
are added here.

### Resolved

Open items from CLAUDE.md, moved here when done, newest first.

- 2026-10-30: Refactoring candidate, nnrpd `ARTpost`: it returned a
  `const char *` (string literals, post.cc's static `Error` buffer, or
  other functions' buffers) that `CMDpost` cut at CR/LF by writing through
  a `const_cast`, safe only because CR/LF came solely from innd's reply in
  the writable buffer.  ARTpost now returns `std::optional<std::string>`
  (empty when accepted); the old body is the file-local `ARTpostMessage`,
  whose return sites are unchanged, and CMDpost trims its own copy.
  Header, definition and caller changed in one commit so that it builds.
  live-innd.sh checks that refused posts (innd's 435, and nnrpd's own
  From-syntax literal) are each one 441 line.
- 2026-10-30: Refactoring candidate, expire/expireover boundary: closed,
  no change.  An article with no Expires header is kept while its arrival
  time is at or after the cutoff (`when >= Default`, expire/expire.cc and
  storage/expire.cc), so a 0-day class keeps articles that arrived in the
  same whole second expire started.  This is upstream INN's behavior;
  changing it would change what existing sites expire, so it stays unless
  the INN community agrees otherwise.  live-innd.sh keeps its `sleep 2`.
- 2026-10-30: CMake-only goal item 1: CMake runs its own platform checks
  (`cmake/InnConfigure.cmake`, with configure's run tests in
  `cmake/checks/`) and writes config.h from `include/config.h.cmake.in`,
  plus inn/system.h, version.h, paths.h and the five inn/portable-*.h
  (`include/CMakeLists.txt`), into the build tree (`8198bbda2` through
  `377c0a4e6`).  libinn's replacement functions are chosen by the checks
  (`a18ea0736`).  Checked with support/compare-config.sh against configure's
  config.h on macOS and Linux: identical except HAVE_STRUCT_TM_TM_GMTOFF,
  where configure is wrong (no <time.h> in its test).  configure's headers
  were moved out of include/; only samples/inn.conf still needs ./configure
  (goal item 3).
- 2026-10-30: CMake-only goal item 2: CMake generates the method tables
  (methods, ovmethods, hismethods) and the ovsqlite and hissqlite SQL
  helpers as C++ in the build tree, rerunning a generator when it or its
  inputs change (`daa978cd1` through `f59b45640`).  The generators write
  `.cc` and take an output directory.  CMake now needs only the three
  headers ./configure writes.  The stale generated .c/.h files a Make build
  had left in the source tree were moved out (they would shadow the
  generated headers through quoted includes).  With this, every source
  compiled into the three libraries is C++.
- 2026-10-30: root checks rerun by the user on macOS after `4780a0b0c`,
  `a885baf6d` and the ovsqlite-server conversion (`30461df12`):
  `sudo support/root-checks.sh` RESULT: PASS, all 34 checks (lib/newsuser
  3/3; rnews, ovsqlite-server x2, innfeed and nnrpd drop to news; the 9
  programs and 19 scripts refuse root).
- 2026-10-30: hissqlite-convert and ovsqlite-server converted to C++
  (`a850c63d9`, `30461df12`), then nullptr (`4aca04be5`, `06b1ce279`),
  void-arg (`441fed69c`) and bool literal (`9a927d98c`).  Tests first: the
  existing hissqlite-convert test had always skipped under verify-cxx.sh
  (`68cfe3743`, `d4d043362`), and ovsqlite-integ.t was not run at all
  (`9ef23a564`, `fb394e519`); coverage 0% → 73.2% and 0% → 39.1%
  (ovsqlite.cc 7.5% → 49.9%).
- 2026-10-30: regression from the security audit (`af8441c0f`) fixed:
  ovsqlite-server and innfeed called ensure_news_user_grp() unconditionally,
  so as any non-root user other than news they died ("can't resolve news to
  a GID", or "must be run as news").  Now called only as root, like rnews
  and nnrpd (`4780a0b0c`, `a885baf6d`).  Found by ovsqlite-integ.t; missed
  by the live harness because build-live's news user is the tester.
- 2026-10-30: tdx-util converted to C++ (`1c1d79bfa`, nullptr `5ece47ac3`;
  nothing for void-arg or bool literals).  Tests first: tests/overview/tdx-util.t
  (`6106a013f`, run by verify-cxx.sh from `eb260b8e9`), 24 checks, coverage
  17.9% → 81.9%.
- 2026-10-30: lib/ replacement functions converted to C++, all 22, one per
  commit (`4c0c33482` through `4cb841856`), then nullptr (`a1e7a91c7`)
  and void-arg (`18bc07d5d`); the bool-literal pass had nothing to change.
  The macOS build compiles none of them; each was checked with
  `c++ -std=c++20 -fsyntax-only`, with and without -DTESTING, and the 14
  with tests are run by verify-cxx.sh.  lib/ now has no `.c` files.
- 2026-10-29: commit-verified.sh left the paths it had staged staged when it
  refused (unstaged changes left, verification or live test failed), so the
  next run committed them too; this merged two ovdb fixes into one commit
  (split again before pushing).  It now restores the index as it was before
  the run whenever it does not commit.
- 2026-10-29: frontends/ converted to C++, all 12 built programs
  (`a1363a6fe` through `efb8bfd9b`), the ovdb tools' Berkeley DB code
  fixed after a syntax check (`57e431e0b`, `74499a1fa`), then nullptr,
  void-arg and bool-literal passes (`3e1e529f4`, `66389a61e`, `e6519ac69`;
  no single-line typedefs).  Tests first: `0380c3f5e`, 50.4% line coverage.
  feedone.c and sys2nf.c stay C for now (see "Other known gaps").
- 2026-10-29: backends/ converted to C++, all 14 files (`2bc8f526b`
  through `c0629da2d`), then nullptr, void-arg, bool-literal and typedef
  passes (`a3fe3102b`, `fd95a8aee`, `85d3bca07`, `0cd2d54e5`).  Tests first:
  `29f5c80e4`, 41.5% line coverage.
- 2026-10-29: nnrpd/ converted to C++, all 18 files (`d298ec362` through
  `5952b3822`), then nullptr, void-arg, bool-literal and typedef passes
  (`624fc7f51`, `e19b4ec63`, `68e73ed90`, `9d6b989d2`).  Tests first: the
  nnrpd section of support/live-innd.sh (`33d778dfa`).
- 2026-10-29: innd coverage gate passed: innd/ 33.9% (from 2.0%) with
  innd/artparse, innd/chan and the live test support/live-innd.sh.  The gate
  (33% before further C++ conversion anywhere, decided 2026-10-28) no longer
  blocks; keep innd above it by adding tests with changes.

### Upstream bugs and quirks found

Behavior of upstream INN found while testing, kept as is unless noted.

- Upstream bug (found 2026-10-30, kept): history/buildconfig, run from the
  top directory, looks for `history/cnfs` (a storage method) instead of
  `history/hisv6`, so it only works when run from history/ (as both builds
  do).
- Upstream behavior (found 2026-10-30): ovsqlite-server copies its socket
  path into `sun_path` with strlcpy, so a `pathrun` that makes it longer
  than 104 bytes (macOS; 108 on Linux) is silently truncated and the
  server listens on a shortened name.  The client truncates the same way,
  so they still meet, but a too-long path should be an error.  verify-cxx.sh
  uses /tmp for its scratch directory because of this (`fb394e519`).
- Upstream bug (found 2026-10-30, kept): `tdx-util` with no mode option says
  "group must be specified for -" instead of "a mode option must be
  specified", because `strchr("cgoOR", mode)` matches the terminating NUL
  when mode is '\0'.  tests/overview/tdx-util.t records the current
  message; fix (and update that check) in the refactoring phase, or report
  upstream.
- Upstream bug, control/gencancel.c: without -n, gencancel copies the
  Newsgroups value from the wire-format spool with its trailing \r, so the
  cancel article it writes has a malformed header that innd refuses.  Same in
  the original C (checked 2026-10-29).  Strip the \r when converting or
  fixing gencancel; support/live-innd.sh works around it.
- Upstream bug, innfeed/article.c:37: `#define VALIDATE_HASH_TABLE() (void(0))`
  is C++ syntax and does not compile as C when NDEBUG is defined.  Latent,
  since NDEBUG is no longer defined; fix when innfeed is converted.
- Kerberos (auth_krb5) is found through `krb5-config`, which on this Mac is
  Anaconda's (`/opt/anaconda3`), matching what configure picked.

### Flaky test: lib/network/client

- Flaky test (2026-10-29): `lib/network/client` once passed 16 of 22 in
  verify-cxx.sh on build/ (refusing a notes-only commit), then 22 of 22 in
  three reruns.  Not investigated; possibly a port or timing race in the
  test's local server.  Note any recurrence here.
  Recurred twice on 2026-10-30 during the lib series (build-asan/, then
  build/), both times 16 of 22 with exit status 0, and 22 of 22 in 14
  reruns by hand, including under full CPU load.  Exit 0 means no check
  failed: six "ok" lines were lost from the captured output (suspect:
  the forked child's output interleaving with the parent's in the pipe).
  verify-cxx.sh now prints a failing test's non-"ok" lines (`2892a1a8a`);
  read them at the next recurrence.
  Recurred 2026-10-30 (generator series, build/), with output: tests 12-18
  fail, all in test_network_read.  The parent accepts a connection, and the
  first network_read on it fails; the errno check expects ETIMEDOUT (60)
  and gets EPIPE (32).  So the accepted connection is already dead: likely
  not the delay_writer child's but a leftover from test_timeout_ipv4, which
  just before hammered the same port 11119 with connection attempts (that
  run timed out at socket 1).  Unconfirmed; a fix would give each section
  its own port or drain the old attempts.  Also unexplained: verify-cxx.sh
  reports exit status 0 although the test prints six "not ok" lines.

2026-10-30: `test_timeout_ipv4` moved to its own port (11120) on the
evidence above; the fix is unconfirmed, since the failure never reproduced
outside verify-cxx.sh (50 back-to-back runs under load passed, old and
new).  If it recurs, the port race was not the cause.

### Coverage history

**Test coverage (support/coverage-report.sh)**
- Measure with `support/verify-cxx.sh build-cov`, then
  `support/live-innd.sh build-cov` (it adds to the profiles; verify-cxx.sh
  clears them), then the report.  Until 33d778dfa live-innd.sh did not set
  `LLVM_PROFILE_FILE`, so a plain run of it counted nothing; earlier figures
  that include it came from profiles left in `build-cov/coverage/raw`, and
  the "before" numbers in that commit's message were stale ones.
- 2026-10-29, fresh profiles, line coverage by directory: innd/ 42.9%,
  nnrpd/ 42.3%, expire/ 39.4%, lib/ 74.4%, history/ 70.3%, storage/ 40.0%,
  authprogs/ 60.4%, control/ 72.1%.
- nnrpd lowest: sasl.cc 6.9% (no SASL mechanism in the tests), misc.cc
  22.5%, commands.cc 32.5%, perm.cc 35.3%.
- 2026-10-29: backends/ 41.5%, frontends/ 50.4% (each measured before its
  conversion).  backends lowest: innbind 8.5%, nntpget 15.4%, innxbatch
  15.9%.  nntpget and innxbatch always connect to port 119 (NNTP_PORT, no
  port option), so only their argument and error paths can run without
  root; a test would need an option for the port or a server on 119.
  frontends lowest: inews 41.0%, ctlinnd 41.2%, rnews 42.1%.
- innd lowest (2026-10-29): nc.cc 21.1% (no NNTP streaming, MODE READER or
  error responses yet), rc.cc 26.2%, site.cc 33.0% (only file and exploder
  feeds), newsfeeds.cc 38.1%.
- 0% also in storage/overdata.cc, lib/inndcomm.cc, lib/network-innbind.cc
  (as of 2026-10-28).
- The report warns "N functions have mismatched data" even with fresh
  profiles; probably sources compiled into more than one binary (tests build
  lib sources with -DTESTING).  Not yet investigated.

### Security work status (2026-10-29 to 2026-10-30)

**Security: no root at run time (decided 2026-10-29; next task after the
coverage gate)**
- Goal: no INN executable runs as root in production; they run as an
  unprivileged account such as `news`.  Tests run as the tester's own user.
- Current state: innd switches to the news user if started as root
  (`ensure_news_user_grp()`, innd.cc), and programs install owned by news
  (0550/0555).  The exception is `innbind`, installed setuid root (4550, group
  news, `LI_SPRI`) solely to bind ports below 1024 for innd and nnrpd.
- Policy refined (2026-10-29): root used only transiently at startup and
  then dropped, or a setuid helper that exits right away, is acceptable.
  innbind meets this: it dies unless run by the news user, binds only the
  allowed ports (119 by default), passes each socket back, and exits
  (backends/innbind.c).  So the post-gate task is an audit: confirm no other
  setuid-root or root-retaining paths (including scripts and install rules),
  that innd drops to news before doing anything else, and add tests for it.
- Audit first pass done 2026-10-29 (CONVERSION_NOTES.md, "Security audit,
  first pass").  innbind, innd, nnrpd -D and rc.news held.  Fixed
  2026-10-29: (1) the privilege drop clears root's supplementary groups;
  (2) nnrpd without -D drops from root; (3) rnews as root drops its group
  too; (4) innfeed and ovsqlite-server drop from root; (5) programs that
  write INN's data refuse root (`ensure_not_root()`; read-only tools are
  exempt).  Started-as-root paths are tested by `support/root-checks.sh`:
  run by hand with sudo on macOS, and as root in Docker on Linux
  (support/docker-root-checks.sh); outputs go in the commit.
- Audit second pass done 2026-10-29: the writer scripts (11 shell, 8 Perl)
  refuse root; contrib/ reviewed (not installed; recommendations recorded);
  fastrm's dead root branch removed.  Remaining: audit the CMake install
  rules when goal item 5 writes them (innbind must stay the only
  setuid-root file).
- Alternatives if ever wanted: systemd socket activation (innd accepts
  `sd_listen_fds` sockets), launchd sockets, Linux `CAP_NET_BIND_SERVICE`.

### Linux and the Make build

10. Portability: build and test on Linux and a BSD, not just macOS.
    Started 2026-10-29: `support/docker-root-checks.sh` builds the tree on
    Debian trixie (aarch64, GCC, GNU ld) in Docker and runs root-checks.sh
    as the container's root.  Fixed on the way: libcrypt for ckpasswd
    (8c93d26fa), the libinnhist/libinnstorage link cycle (ef4885a92).  Not
    yet run there: verify-cxx.sh and live-innd.sh.
    2026-10-30, fresh-clone check: a clone plus BUILDING.md's dependencies
    now builds and passes verify-cxx.sh and live-innd.sh on macOS and on
    Debian (support/docker-linux-checks.sh, as an ordinary user).  Fixed:
    samples/inn.conf generated by CMake (the stale fixconfig had left
    `pathhistory: @HISTORYDIR@`); GNU nm in verify-cxx.sh; libinnstorage
    named again after libinnhist for GNU ld; innd's message when a news
    group exists (Debian); BSD `sed -i ''`, `$'\r'` under dash and xcrun in
    the harnesses; innfeed's status file changes only on connection
    events, so the streaming check tests refusals, not a re-read.  Tagged
    v2.8.0-cxx.alpha.1 (GitHub pre-release) as the starting point for
    public testing.
- The Autoconf/Make build can no longer build libinn, libinnhist or
  libinnstorage (their Makefiles list `.c` files that are now `.cc`).  CMake
  builds all 49 programs (2026-10-28).  As of 2026-10-29 every program in
  innd/, nnrpd/, backends/, frontends/, expire/, control/ and authprogs/
  (except auth_krb5) is run by a test; innfeed and the storage/history
  tools other than tdx-util and makehistory are not yet.  (2026-10-30:
  tdx-util, hissqlite-convert and ovsqlite-server are now run by tests and
  converted; see Resolved.)
- The Make build's storage/ and history/ Makefiles still name the generated
  sources `.c` (`methods.c`, `sql-*.c`, ...); the generators now write `.cc`.
  Those libraries were already unbuildable with Make (next item).

### Other findings

- Learned: offering a peer an article that is missing from the spool makes
  innfeed send an empty article (a bare "."), which hung tinyleaf; tests
  must feed articles that exist.  innfeed keeps reading its channel input
  until EOF, so the test holds the input open until delivery finishes.
- ident's INN test needs an identd on port 113 (tests 2 and 4); the new
  authprogs/identport test covers ident with a stand-in server instead.

### Verification set (as of 2026-10-30)

Current verification set:
- CMake build of lib, history, storage and innd succeeds
- `innd` exports every symbol the Makefile-built `innd` does (169)
- No C++-mangled exported symbols in `innd`, `libinn.a`, `libinnhist.a` or
  `libinnstorage.a` (C linkage kept)
- `innd -h` starts and reaches the news-group check
- `tinyleaf` smoke test: HELP, IHAVE, duplicate IHAVE, unknown command, QUIT
- INN tap tests built against the CMake libraries: all 49 C-based `lib/`
  tests in `tests/TESTS` (replacement-function tests compile their
  `lib/X` source with `-DTESTING`, from `.cc` once converted),
  `expire/tombstone-hisexpire`, `overview/api`,
  `overview/buffindexed`, `overview/tradindexed`, `overview/ovsqlite`,
  `overview/tdx-group`, `overview/xref`, `storage/caf`,
  `storage/cancel-tombstone`, `storage/methods` (cnfs, timecaf, timehash and
  trash store/retrieve/cancel; written for this conversion)

Not yet covered: shell-driven tests that need INN's command-line programs
(`overview/overchan`, `storage/sm`,
`storage/archive`, `storage/makehistory`, `lib/hissqlite-util`,
`lib/xmalloc`), until CMake builds those programs.
`lib/canlock` reports SKIP (no libcanlock); SKIP does not fail
verification.  `lib/hissqlite-convert` and `overview/ovsqlite-integ` find
the CMake-built programs through HISSQLITE_CONVERT and OVSQLITE_SERVER,
which verify-cxx.sh sets (2026-10-30).
`samples/inn.conf` (needed by `lib/innconf`) is generated from
`inn.conf.in`; verify-cxx.sh runs `make -C samples inn.conf` if it is missing.

Add the relevant INN tests when converting a new area (e.g. storage tests for
`storage/`). Commits before this rule have retroactive `git notes`
(`git log --notes`) recording checks run after the fact.
