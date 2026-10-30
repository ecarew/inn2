# INN++ C++ Design Rules

This project is converting the INN 2.8.0 C codebase to C++. All new and converted
code must follow the rules below.

---

## Build system

- CMake minimum version: 4.3
- C++ standard: C++20 for every C++ target, set once in the root CMakeLists.txt
  (`CMAKE_CXX_STANDARD 20`, `..._REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`).
  Check `-std=c++20` appears in `build/compile_commands.json` for all `.cc`.  C++20 has far fewer gaps than C++23 in Apple's
  libc++; don't use C++23-only features, or the C++20 features libc++ lists as
  incomplete (modules, chrono time zones, `jthread`, execution policies).
  Details and sources: CONVERSION_NOTES.md, "Build system".
- C linkage comes from `BEGIN_DECLS`/`END_DECLS` in the headers, not from
  `extern "C"` blocks in source files (see "Inclusion of C headers").
- Per-file compile flags (Perl, Python) use `set_source_files_properties … COMPILE_OPTIONS`

## Inclusion of C headers

INN headers declare their contents between `BEGIN_DECLS` / `END_DECLS`, which
expand to `extern "C" {` / `}` under C++. Converted `.cc` files therefore
include headers exactly as the C version did, with no `extern "C"` blocks in
source files. That keeps functions defined in `.cc` files callable from the
remaining `.c` files. If a header lacks the guards, add `BEGIN_DECLS` /
`END_DECLS` to the header rather than wrapping its `#include`.

## Conversion workflow

One file per commit: `git mv foo.c foo.cc`, update the source list in that
directory's `CMakeLists.txt`, build, fix only what the compiler reports, then
commit. Modernization (classes, containers, RAII) goes in separate commits
after the file compiles as C++.
When a directory is fully converted, run the automatic modernization passes
on its `.cc` files as separate commits (`support/cxx-tools/tidy-fix.sh`:
`modernize-use-nullptr`, `modernize-redundant-void-arg`,
`modernize-use-bool-literals`; single-line typedefs to `using` by hand, since
clang-tidy 23.1.4's fix corrupts anonymous structs).  The deferred passes
(C-style casts, init-variables, implicit bool) stay on hold.

## Testing before commit

Run the full verification set *before* every commit and record it in the
commit message as a `Tested:` section listing each check and its result.
Only list checks that actually ran on the exact tree being committed.

Run `support/verify-cxx.sh` (needs a configured `build/`) and paste its output
into the `Tested:` section.  `support/cxx-tools/commit-verified.sh` does both;
the conversion helpers are listed in `support/cxx-tools/README.md`.  The
reference data lives in `support/verify-cxx/`.

Live innd test: configure a test build with
`cmake -S . -B build-live -DINN_RUNAS_USER=$(id -un) -DINN_RUNAS_GROUP=$(id -gn)`
and run `support/live-innd.sh build-live`; commit-verified.sh runs it when
build-live/ exists.  Never set INN_RUNAS_* on a production build.

Also run it against a sanitizer build when a change touches memory handling:
`cmake -S . -B build-asan -DINN_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`, then
`support/verify-cxx.sh build-asan` (AddressSanitizer + UBSan; the script builds
the tests with the same flags).  `compile_commands.json` is always written to
the build directory for clang-tidy (Homebrew LLVM: `/opt/homebrew/opt/llvm/bin`).

Code coverage: `cmake -S . -B build-cov -DINN_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug`,
`support/verify-cxx.sh build-cov`, then `support/coverage-report.sh build-cov`
(add `--html` for a line-by-line report in `build-cov/coverage/html/`).  Uses
Xcode's `llvm-profdata`/`llvm-cov` via `xcrun`, matching Apple clang.

Source browser with call/caller graphs: `doxygen support/Doxyfile` (Doxygen +
Graphviz), output in `build-doxygen/html/index.html` (about 8 s, 320 MB).

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
(`overview/ovsqlite-integ`, `overview/overchan`, `storage/sm`,
`storage/archive`, `storage/makehistory`, `lib/hissqlite-util`,
`lib/xmalloc`), until CMake builds those programs.
`lib/canlock` and `lib/hissqlite-convert` report SKIP (no libcanlock; the
hissqlite-convert program is not built); SKIP does not fail verification.
`samples/inn.conf` (needed by `lib/innconf`) is generated from
`inn.conf.in`; verify-cxx.sh runs `make -C samples inn.conf` if it is missing.

Add the relevant INN tests when converting a new area (e.g. storage tests for
`storage/`). Commits before this rule have retroactive `git notes`
(`git log --notes`) recording checks run after the fact.

## Class design rules

Rationale and the existing C object patterns are in CONVERSION_NOTES.md,
"Class design approach".  In order:

1. Map the layers and the ownership first: who creates each object, who
   destroys it, who borrows it (Doxygen include and dependency graphs).
2. Look for INN's existing C object patterns (method tables, opaque handles,
   prefix-named functions, callback structs, globals) before inventing classes.
3. Collect each candidate's operations from Doxygen's "referenced by" lists
   and call graphs.  The interface is those operations; a getter or setter
   exists only where callers genuinely need one.
4. Separate value types (small, copyable, often immutable: `ArtHeader`,
   `TOKEN`) from entity types (identity and resources: a channel, an open
   history database; movable, not copyable).
5. State invariants and ownership: the constructor establishes them, the
   destructor releases resources, standard containers make most classes Rule
   of Zero.
6. C compatibility only where an external consumer demonstrably needs it
   (Perl/Python embedding, external users of libinn).  Otherwise a header may
   become C++-only once its last in-tree C user is converted.
7. Test first: no refactoring without measured coverage; add tests first.

## Conversion notes

Record every design decision, with its reasoning and the alternatives
considered, in `CONVERSION_NOTES.md`, and reference the commits.  It is the
reviewer-facing record of why the C++ differs from upstream INN; keep it
current in the same commit as the change it describes.

## Open items

Deferred work, newest first within each group.  When an item is done, move
it to "Resolved" below with the date and commit rather than deleting it.

**Goal: CMake-only build, no Autoconf/Make left (decided 2026-10-28)**
Everything below must be done before the Autoconf/Make build is deleted.
1. Native configuration: replace `./configure` with CMake checks
   (`check_include_file`, `check_symbol_exists`, `find_package`, options for
   optional features) and generate `config.h`, `inn/paths.h`, `inn/system.h`
   and friends with `configure_file()`.  Today CMake only reads configure's
   `config.h`.
2. Generated sources: run `storage/buildconfig`, `history/buildconfig` and
   `lib/sqlite-helper-gen` (or CMake equivalents) from CMake with
   `add_custom_command`, instead of requiring one Make pass first.
3. Generated configuration and helpers: `samples/*` (inn.conf etc. via
   fixconfig) and `support/fixconfig`, `support/fixscript`.
4. Scripts: generate the shell and Perl programs from their `.in` files
   (controlchan, pgpverify, nntpsend, mailpost, innreport, inncheck,
   expirerm, ...).
5. Installation: install rules for programs, scripts, libraries, headers and
   samples, with INN's ownership, setuid and permission modes.
6. Documentation: man pages generated from `doc/pod`.
7. Perl module `perl/INN/` (INN::Config, ovsqlite_client) and `contrib/`.
8. Optional features as CMake options: Perl and Python filters/hooks,
   Berkeley DB (ovdb), OpenSSL/TLS, libcanlock, systemd, blocklist.
9. Tests: run them through CTest instead of only support/verify-cxx.sh.
10. Portability: build and test on Linux and a BSD, not just macOS.
    Started 2026-10-29: `support/docker-root-checks.sh` builds the tree on
    Debian trixie (aarch64, GCC, GNU ld) in Docker and runs root-checks.sh
    as the container's root.  Fixed on the way: libcrypt for ckpasswd
    (8c93d26fa), the libinnhist/libinnstorage link cycle (ef4885a92).  Not
    yet run there: verify-cxx.sh and live-innd.sh.

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

**Comparative analysis, original C vs C++ (asked 2026-10-29)**
Two rounds: (1) when the conversion is complete, before refactoring;
(2) again after the modern-C++ refactoring phase.  Same machine, compiler and
optimization flags for both sides; original C built from `main` with its
Autoconf/Make build, C++ from `cmake-build`.  Record method and raw numbers in
a document so the rounds can be compared.
- Lines of code written by the developers only: exclude machine-generated
  code (configure output, `config.h`, buildconfig and sqlite-helper-gen
  output, bison/flex output, the Perl `.in` scripts' substitutions) and
  count comments and blank lines separately.
- Size of the generated executables and libraries (stripped and unstripped,
  `size` text/data/bss).
- Performance from the tests: wall-clock time of each test run and of the
  live innd test, and peak memory (`/usr/bin/time -l` maximum resident set
  size on macOS); repeat runs and report the spread, not a single number.

**Refactoring candidates (after the C++ and CMake conversion)**
- expire/expireover timing (pinned by the user, 2026-10-29): articles expire
  only when strictly older than "now", measured in whole seconds, so with a
  0-day limit an article that arrived in the current second is kept.  Decide
  whether that is intended and whether finer-grained or inclusive time
  comparison is wanted; then drop the `sleep 2` in support/live-innd.sh that
  works around it.
- nnrpd `ARTpost` returns `const char *` that `CMDpost` truncates in place,
  through a `const_cast` that is safe only because of where CR/LF can come
  from (CONVERSION_NOTES.md).  Return an owned string instead.
- Retire ovdb and the Berkeley DB dependency (proposed 2026-10-29; reasons,
  sources and plan in CONVERSION_NOTES.md, "Retiring ovdb and Berkeley DB").
  Condition: first ship a one-way converter from existing ovdb databases to
  ovsqlite that does not itself depend on Berkeley DB.  Whether Berkeley DB
  files can be read without its library is open: prototype first; fallbacks
  are parsing `db_dump` text output or rebuilding with `makehistory -O`.
  Decide ckpasswd's Berkeley DB ndbm use separately.  Raise with upstream
  INN before removing anything.

**Housekeeping (later)**
- Once the class work no longer needs them as references, remove the
  "Branches kept for reference" bullet from CONVERSION_NOTES.md and delete
  the `cxx-conversion-wip` and `legacy-2020` branches, locally and on GitHub
  (asked 2026-10-29).  `cxx-conversion-wip` has the first `ArtHeader` /
  `ARTfields` attempt; `legacy-2020` the 2020 storage renames.  Deleting a
  pushed branch is irreversible for others: confirm with the user first.

**Documentation (2026-10-28)**
- Convert `readme.pod` to Markdown (`README.md`), next to `CONVERSION_NOTES.md`.

**Design follow-ups (2026-10-28)**
- Convert innfeed's parser to the C++ outputs of bison (lalr1.cc) and flex
  (C++ scanner) when innfeed is converted.
- Benchmark the current header tree against the planned sorted-array
  `HeaderTable` with the 76 real header names (see CONVERSION_NOTES.md); keep
  it as the first test of `HeaderTable`.

**innfeed (2026-10-29; deferred, to come back to)**
- support/live-innd.sh has an innfeed section: -C on a good and a broken
  configuration, delivery to an IHAVE-only peer (tinyleaf behind a Perl
  listener), streaming to innd (duplicates refused), and spooling to the
  backlog with the peer down.  Statistics come from the -S status file;
  innfeed sends them to syslog otherwise.  Coverage not yet measured.
- Open 1: offering tinyleaf articles it already has.  After the first
  duplicate reply innfeed puts the connection to sleep and spools the rest
  ("offered 1, requeued 1, spooled 3"); it does not treat tinyleaf's reply
  as a refusal.  Find which side is wrong (tinyleaf's reply code or
  innfeed's handling).
- Open 2: replaying a backlog with innfeed -x after renaming
  `<peer>.output` to `<peer>` offered nothing ("offered 0") and exited.
- Learned: offering a peer an article that is missing from the spool makes
  innfeed send an empty article (a bare "."), which hung tinyleaf; tests
  must feed articles that exist.  innfeed keeps reading its channel input
  until EOF, so the test holds the input open until delivery finishes.
- imapfeed (imap_connection.c, a quarter of innfeed/) needs an IMAP/LMTP
  stand-in to test.  Then the conversion: tests first, 33% gate.

**authprogs (2026-10-29)**
- auth_krb5.c is the only authprogs file not converted: it has no test, and a
  meaningful one needs a Kerberos KDC (e.g. a scratch MIT krb5kdc with a test
  realm and keytab).  Until then only argument and error paths could be
  tested.  Its Kerberos is the Anaconda krb5 found by krb5-config.  Run the
  modernization passes on authprogs/ once it is converted.
- ident's INN test needs an identd on port 113 (tests 2 and 4); the new
  authprogs/identport test covers ident with a stand-in server instead.

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

**Deferred modernization (on hold by decision, 2026-10-28)**
- C-style casts: 323 `cppcoreguidelines-pro-type-cstyle-cast` hits in the
  `.cc` files.  Needs case-by-case judgment (static/const/reinterpret_cast).
- Uninitialized variables: 2430 `cppcoreguidelines-init-variables` hits.
  Mostly C-style declarations; noisy, and fixes can change behavior.
- Implicit bool conversions: 786 `readability-implicit-bool-conversion`
  hits (`if (ptr)`, `while (n)`).  Style; low priority.

**Other known gaps (2026-10-28)**
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
- 49 struct/enum `typedef`s with bodies left by `modernize-use-using`; convert
  to `struct X { ... };` during struct/class modernization.  Do not use
  clang-tidy 23.1.4's `-fix` for this check: it corrupts anonymous struct
  typedefs (see commit 085d0741d).
- Code never compiled as C++ because its feature is off in this build:
  `innd/perl.cc`, `innd/python.cc`, `lib/perl.cc` (DO_PERL/DO_PYTHON),
  `storage/ovdb/ovdb.cc` (HAVE_BDB), `lib/canlock.cc` (libcanlock).  Also
  where `NULL` still remains in compiled-out code.  `nnrpd/tls.cc`,
  `nnrpd/perl.cc` and `nnrpd/python.cc` passed a syntax-only compile with
  their feature defined (CONVERSION_NOTES.md, "Code compiled out in this
  build") but are not linked or run.  The same check would help the innd
  and lib files above.  The frontends ovdb tools passed it too, against
  Homebrew's Berkeley DB 18 (`brew install berkeley-db`, installed
  2026-10-29; use `-I/opt/homebrew/opt/berkeley-db/include`, not
  `-idirafter`, or the SDK's Berkeley DB 1.85 `db.h` wins).
- `storage/ovdb/ovdb.cc` fails that check: with `-DHAVE_BDB` it has many
  errors, starting with a keyword name near line 1777 and broken
  declarations from line 2664 (checked 2026-10-29; not yet fixed).  Fix it
  before ovdb can be turned on (goal item 8).
- `frontends/feedone.c` and `frontends/sys2nf.c` are still C.  The Makefile
  lists them as OPTIONAL (built only on request) and CMake does not build
  them, so there is nothing to test them against; convert them once they
  are built and tested (e.g. as targets excluded from the default build).
- Generated sources still C: `storage/methods.c`, `storage/ovmethods.c`,
  `storage/ovsqlite/sql-*.c`, `history/hismethods.c`,
  `history/hissqlite/hissqlite-{init,main,read}.c` (their generators: goal
  item 2).
- lib fallbacks vs the C++ standard library (2026-10-30): check which of
  the 22 replacement functions (asprintf, snprintf, strlcpy, strlcat,
  strcasecmp, strspn, strtok, setenv, mkstemp, pread, pwrite, fseeko,
  ftello, getpagesize, seteuid, symlink, daemon, inet_aton, inet_ntoa,
  inet_ntop, getaddrinfo, getnameinfo) the C++ standard library covers
  (`std::snprintf`, `std::format`, `std::string`, `std::filesystem`, ...),
  and whether those fallbacks and their HAVE_* checks can be retired.
  Cite the libc++/cppreference status pages.  The POSIX ones (networking,
  daemon, seteuid, mkstemp, fseeko/ftello) have no standard C++
  equivalent.  For pread/pwrite, `std::spanstream` (memory only) and
  Boost.Iostreams (seek then read, moves the file offset) do not match;
  Boost.Asio's `random_access_file::read_some_at`/`write_some_at` does,
  at the cost of a Boost dependency.  macOS and Linux both have all 22,
  so dropping a fallback may need no replacement at all.
- The Autoconf/Make build can no longer build libinn, libinnhist or
  libinnstorage (their Makefiles list `.c` files that are now `.cc`).  CMake
  builds all 49 programs (2026-10-28).  As of 2026-10-29 every program in
  innd/, nnrpd/, backends/, frontends/, expire/, control/ and authprogs/
  (except auth_krb5) is run by a test; innfeed and the storage/history
  tools other than tdx-util and makehistory are not yet.

- Kerberos (auth_krb5) is found through `krb5-config`, which on this Mac is
  Anaconda's (`/opt/anaconda3`), matching what configure picked.
- Upstream bug, control/gencancel.c: without -n, gencancel copies the
  Newsgroups value from the wire-format spool with its trailing \r, so the
  cancel article it writes has a malformed header that innd refuses.  Same in
  the original C (checked 2026-10-29).  Strip the \r when converting or
  fixing gencancel; support/live-innd.sh works around it.
- Upstream bug, innfeed/article.c:37: `#define VALIDATE_HASH_TABLE() (void(0))`
  is C++ syntax and does not compile as C when NDEBUG is defined.  Latent,
  since NDEBUG is no longer defined; fix when innfeed is converted.
- Shell-driven tests not yet run: `overview/ovsqlite-integ`,
  `overview/overchan`, `storage/sm`, `storage/archive`, `storage/makehistory`,
  `lib/hissqlite-util`, `lib/xmalloc` (need the programs above).

**Decisions**
- 2026-10-29: INN++ must compile and pass its tests on both macOS and
  Linux.  Platform differences are chosen by feature tests, not platform
  names: the code tests `HAVE_*` macros from `config.h` (written by
  configure today; by CMake checks such as `check_symbol_exists` through
  `configure_file` once configure is gone, goal item 1), and CMake chooses
  link libraries from its own checks (e.g. libcrypt only where `crypt()` is
  not in libc).  Avoid `if(APPLE)` and `#ifdef __APPLE__`/`__linux__` except
  for a real quirk no feature test can detect, and say why where used.
  Linux is built and checked with support/docker-root-checks.sh (Debian
  trixie in Docker).
- 2026-10-29: INN's test programs in `tests/` stay C, as a standing check of
  the C/C++ boundary and so the same test source runs against the original C
  and the C++ builds (CONVERSION_NOTES.md, "Not converted").  New tests are
  written in C too.
- 2026-10-29: CONVERSION_NOTES.md records the conversion only.  C++-specific
  design and refactoring beyond it (classes, memory management, containers)
  gets a separate document, started after the conversion is complete and
  working to both the user's and Claude's satisfaction.  Its "Planned design"
  section is the seed for that document.
- 2026-10-28: INN's Perl and Python code is not converted to C++.  The Python
  in `samples/` and the embedded Perl/Python filter and auth hooks are a
  site-extension interface; the ~25,600 lines of Perl admin scripts are
  text-processing tools better left in Perl; `INN::Config` is part of INN's
  Perl interface.  They only need generating, building and installing
  (CMake-only goal items 4, 7 and 8).

**Resolved**
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

## void* allocations

Use `inn_void_ptr` (defined in `include/inn/xmalloc.h`) — `xmalloc`, `xcalloc`,
`xrealloc`, and `xreallocarray` already return it. No explicit casts needed at
call sites; the template conversion operator handles the assignment.

---

## Scott Meyers — Effective C++ (3rd ed.) applicable items

**Resource management (RAII)**
- Item 13: Use objects to manage resources. Acquire in constructor, release in
  destructor. Prefer `std::unique_ptr` / `std::shared_ptr` over raw `new`/`delete`.
- Item 14: Copy constructors and copy-assignment in resource-managing classes must
  decide between deep copy and reference-counting. Document the choice.
- Item 15: Resource-managing classes should provide a way to access the raw resource
  (e.g., `.get()` or an implicit conversion), so they compose with C APIs.
- Item 17: Store the result of `new` in a smart pointer in the same statement —
  never interleave it with other expressions that could throw.

**Class design**
- Item 3: Use `const` wherever semantically correct — member functions, parameters,
  local variables, pointers-to-const. Prefer `const T&` return for members that
  expose internals.
- Item 7: Declare destructors `virtual` in any class intended as a base for
  polymorphic use.
- Item 8: Destructors must not propagate exceptions. Catch and swallow (or
  `std::terminate`) inside the destructor; never let one escape.
- Item 9: Never call virtual functions from constructors or destructors — the
  derived type is not yet (or no longer) alive.
- Item 20: Prefer `const T&` over `T` for parameters that are not going to be
  modified and are not trivially cheap to copy.
- Item 23: Prefer non-member, non-friend functions over member functions when
  the implementation does not require private access. Keeps the interface minimal.
- Item 27: Minimize casting. When a cast is unavoidable, use the C++ named casts
  (`static_cast`, `reinterpret_cast`, `const_cast`). Never C-style `(T)expr`.

---

## Scott Meyers — Effective Modern C++ applicable items

**Type deduction and auto**
- Item 5: Prefer `auto` over explicit type declarations for variables whose type
  is obvious from context or verbose to spell out.
- Item 8: Use `nullptr` instead of `0` or `NULL` for null pointer values.
- Item 9: Prefer alias declarations (`using Foo = …`) over `typedef`.
- Item 10: Prefer scoped enumerations (`enum class`) over unscoped `enum`.

**Function and class declarations**
- Item 11: Prefer `= delete` over `private` + undefined for functions you want to
  prohibit. Apply to copy/move operations that should not exist.
- Item 12: Mark every overriding virtual function `override`. The compiler then
  catches signature mismatches.
- Item 14: Declare a function `noexcept` if it genuinely will not throw — move
  constructors and move-assignment operators in particular (see Caisse rules below).
- Item 15: Use `constexpr` for values and functions that can be evaluated at
  compile time. Prefer it over `#define` for constants.

**Special member function generation (Rule of Five)**
- Item 17: The compiler generates default constructor, destructor, copy ctor, copy
  assignment, move ctor, and move assignment — but only under specific conditions.
  Declaring *any* of destructor, copy ctor, or copy assignment suppresses move
  generation. See Caisse rules for the canonical form.

**Smart pointers**
- Item 18: Use `std::unique_ptr` for exclusive-ownership resources. Zero overhead
  over raw pointer; move-only semantics make ownership explicit.
- Item 19: Use `std::shared_ptr` only when shared ownership is genuinely required.
  Prefer `unique_ptr`; convert to `shared_ptr` when callers need it.
- Item 21: Prefer `std::make_unique` and `std::make_shared` over `new` directly —
  exception-safe, avoids repeating the type name.

**Lambdas**
- Item 31: Avoid default capture (`[=]`, `[&]`). Capture only what you need,
  by name, so the intent is explicit.

---

## Michael Caisse — The Canonical Class (CppCon 2014)

The six special member functions and their canonical forms:

```
Default constructor   T()
Destructor            ~T()
Copy constructor      T(const T&)
Copy assignment       T& operator=(const T&)
Move constructor      T(T&&) noexcept
Move assignment       T& operator=(T&&) noexcept
```

**Rule of Zero** — the preferred option for new classes:  
If every data member is itself a well-behaved type (owns its resource via RAII),
declare none of the six. The compiler-generated versions do the right thing.
Document that this is intentional with a comment if the class looks non-trivial.

**Rule of Five** — required when you manage a raw resource:  
If you declare *any* of {destructor, copy ctor, copy assignment, move ctor,
move assignment}, you must declare *all five*. Omitting any invites subtle bugs
or silently degraded performance.

**noexcept rules**
- Move constructor: `noexcept` — required for standard containers to use move
  instead of copy when they resize.
- Move assignment: `noexcept` — same reason.
- Copy constructor: not `noexcept` — may allocate.
- Copy assignment: not `noexcept` — may allocate.
- Destructor: implicitly `noexcept`; never mark `noexcept(false)`.

**Canonical move constructor**

```cpp
T(T&& other) noexcept
    : member_(std::move(other.member_))
{
    // leave other in a valid, destructible state
}
```

**Assignment: pick ONE of the two forms below, never both.** A by-value
`operator=(T)` alongside `operator=(T&&)` makes rvalue assignment ambiguous and
fails to compile. Either write separate `operator=(const T&)` + `operator=(T&&) noexcept`,
or write the single by-value copy-and-swap operator, which serves both copy and move.

**Canonical move-assignment operator** (separate-overloads form)

```cpp
T& operator=(T&& other) noexcept {
    if (this != &other) {
        member_ = std::move(other.member_);
    }
    return *this;
}
```

**Canonical unified assignment (copy-and-swap idiom)** — replaces both overloads

```cpp
T& operator=(T other) {   // pass by value: caller's copy
    swap(*this, other);
    return *this;
}
friend void swap(T& a, T& b) noexcept {
    using std::swap;
    swap(a.member_, b.member_);
}
```

**Explicit-delete pattern for non-copyable types**

```cpp
T(const T&) = delete;
T& operator=(const T&) = delete;
```

---

## General style guidance

- Prefer `std::string` and `std::string_view` over raw `char *` in new code.
  Existing C-interface boundaries keep `const char *`.
- Prefer range-based `for` over index loops where the index itself is not needed.
- Keep `#include` of C headers inside `extern "C"` blocks; `#include` of C++
  standard-library headers outside, above the `extern "C"` block.
- Do not use `using namespace std` at file scope.
