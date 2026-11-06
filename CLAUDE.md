# INN++ C++ Design Rules

This project converts the INN 2.8.0 C codebase to C++20 with a CMake-only
build.  All new and converted code follows the rules below.  The reasoning
behind them, and the history of the work, are in
[CONVERSION_NOTES.md](CONVERSION_NOTES.md).

---

## Build system

- CMake minimum version 4.3.  C++20 for every C++ target, set once in the
  root CMakeLists.txt (`CMAKE_CXX_STANDARD 20`, `..._REQUIRED ON`,
  `CMAKE_CXX_EXTENSIONS OFF`).  Don't use C++23-only features, or the C++20
  features libc++ lists as incomplete (modules, chrono time zones,
  `jthread`, execution policies).
- CMake runs its own platform checks (`cmake/InnConfigure.cmake`) and
  generates config.h and the other headers (`include/CMakeLists.txt`) and
  the method tables and SQL helpers (storage/, history/) into the build
  tree.  Nothing generated lives in the source tree: a stale copy there
  would shadow the build's through a quoted `#include`.
- macOS and Linux must both build and pass the tests.  Choose platform
  differences by feature tests (`HAVE_*` from CMake checks), never by
  platform name (`if(APPLE)`, `__linux__`), except for a quirk no test can
  detect, with a comment saying why.
- Per-file compile flags (Perl, Python) use
  `set_source_files_properties … COMPILE_OPTIONS`.
- Boost 1.92 or later is required (decided 2026-10-30), header-only
  libraries only until a compiled one is agreed; link `Boost::headers`.
  Prefer the standard library, then Boost, then INN's own code.

## Inclusion of C headers

INN headers declare their contents between `BEGIN_DECLS` / `END_DECLS`, which
expand to `extern "C" {` / `}` under C++.  Converted `.cc` files include
headers exactly as the C version did, with no `extern "C"` blocks in source
files, so functions defined in `.cc` files stay callable from the remaining
`.c` files.  If a header lacks the guards, add them to the header rather
than wrapping its `#include`.

## Conversion workflow

- Tests first: before a directory is converted, every program CMake builds
  there is run by a test and the directory reaches 33% line coverage.
- One file per commit: `support/cxx-tools/convert-file.sh <dir> <path>`
  (`git mv`, CMakeLists.txt, keyword renames, build), fix only what the
  compiler reports (`autocast.sh` for `void *` conversions), commit.
- After a directory is converted, run the modernization passes as separate
  commits with `support/cxx-tools/tidy-fix.sh`: `modernize-use-nullptr`,
  `modernize-redundant-void-arg`, `modernize-use-bool-literals`; single-line
  typedefs to `using` by hand (clang-tidy 23.1.4's fix corrupts anonymous
  structs).  C-style casts, init-variables and implicit bool stay on hold.
- `void *` from `xmalloc`, `xcalloc`, `xrealloc` and `xreallocarray` needs no
  cast: they return `inn_void_ptr` (include/inn/xmalloc.h).
- Not converted: INN's Perl and Python, and the test programs in `tests/`,
  which stay C (new tests are written in C too).  The exception is a test
  that includes a program's internal headers (innd/innd.h, nnrpd/nnrpd.h,
  the tradindexed, buffindexed and timecaf private headers): it is C++, so
  those headers can use C++ types (architect, 2026-11-04;
  CONVERSION_NOTES, "Internal tests in C++").

## Testing before commit

Run the verification *before* every commit and record it in the commit
message as a `Tested:` section listing each check and its result, only for
checks that ran on the exact tree committed.
`support/cxx-tools/commit-verified.sh <subject> <body-file> [paths]` (with
`COMMIT_TRAILER` set) runs `support/verify-cxx.sh` and the live test, and
commits only if they pass; it refuses if unstaged changes remain.

| Build | Configure | Run |
|---|---|---|
| build | `cmake -S . -B build -G Ninja` | `support/verify-cxx.sh` |
| build-live | `-DINN_RUNAS_USER=$(id -un) -DINN_RUNAS_GROUP=$(id -gn)` | `support/live-innd.sh build-live` (commit-verified.sh runs it) |
| build-asan | `-DINN_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug` | `support/verify-cxx.sh build-asan`, for memory-handling changes |
| build-cov | `-DINN_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug` | `verify-cxx.sh build-cov`, then `live-innd.sh build-cov`, then `support/coverage-report.sh build-cov [--html]` |

- Configure every build directory with `-G Ninja` (the other rows add their
  options to the first row's command).  ccache is used automatically when
  installed (`INN_CCACHE`).
- Never set INN_RUNAS_* on a production build.
- Root paths: `sudo support/root-checks.sh` (the user runs it on macOS;
  output goes in the commit), and `support/docker-root-checks.sh` on Linux
  (Debian trixie in Docker; archives tracked files only).
- `support/compare-config.sh` compares CMake's config.h with a configure
  one, symbol by symbol.
- Doxygen call graphs: `doxygen support/Doxyfile` → `build-doxygen/html/`.
- Add tests for any area a change touches.  clang-tidy uses Homebrew LLVM
  (`/opt/homebrew/opt/llvm/bin`) and `build/compile_commands.json`.

## Class design rules

Rationale and INN's existing C object patterns: CONVERSION_NOTES.md, "Class
design approach".  In order:

1. Map the layers and the ownership first: who creates each object, who
   destroys it, who borrows it (Doxygen include and dependency graphs).
2. Look for INN's existing C object patterns (method tables, opaque handles,
   prefix-named functions, callback structs, globals) before inventing classes.
3. Collect each candidate's operations from Doxygen's "referenced by" lists
   and call graphs; add a getter or setter only where callers need one.
4. Separate value types (small, copyable: `ArtHeader`, `TOKEN`) from entity
   types (identity and resources: a channel, an open history database;
   movable, not copyable).
5. State invariants and ownership: the constructor establishes them, the
   destructor releases resources, standard containers make most classes Rule
   of Zero.
6. C compatibility only where an external consumer needs it (Perl/Python
   embedding, external users of libinn).
7. No refactoring without measured coverage; add tests first.

## Notes discipline

- Every design decision, with its reasoning and the alternatives, goes in
  CONVERSION_NOTES.md in the same commit as the change, with commit
  references.  It records the conversion only; C++ redesign beyond it gets a
  separate document, started after the conversion is complete.
- Open items live below.  When one is done, move it to the dated
  "Resolved" log in CONVERSION_NOTES.md ("Conversion log") with its commits;
  don't delete it.  Findings and history go there too, not here.

---

## Open items

**CMake-only goal: no Autoconf/Make left (decided 2026-10-28)**
Items 1 (native configuration), 2 (generated sources), 3 (samples), 4
(scripts), 5 (install rules, 2026-11-04) and 9 (CTest: `ctest --test-dir build`, tests/CMakeLists.txt) are
done.  Generated files come from cmake/InnPaths.cmake (directories,
configure's substitutions, inn_fixscript/inn_scripts); support/fixconfig
and support/fixscript are Make-only now.
5. Install: done.  Left: man pages, docs and filter samples (with item 6
   and 8); support/docker-install-checks.sh must keep passing (innbind the
   only setuid file).
6. Man pages from `doc/pod`.
7. Perl module `perl/INN/` (INN::Config, ovsqlite_client).  `contrib/` done
   (2026-11-03): converted, improved, tested, built with `-DINN_CONTRIB=ON`
   (off by default).
8. Optional features as CMake options: Perl and Python hooks, Berkeley DB
   (ovdb), OpenSSL/TLS, systemd, blocklist (libcanlock done 2026-11-01:
   found when present).  Before ovdb can be
   on, fix `storage/ovdb/ovdb.cc` (fails a `-DHAVE_BDB` syntax check).
10. Portability: a BSD.  Linux runs everything (verify-cxx.sh,
    live-innd.sh, root checks) via `support/docker-linux-checks.sh` and
    `support/docker-root-checks.sh` (2026-10-30).
11. Install checks on local VMs (decided 2026-11-04 with the user: local,
    not AWS): Lima for Linux (systemd unit, cron news.daily, restart after
    reboot), QEMU with the FreeBSD arm64 cloud image for item 10.  Needs
    `brew install lima qemu` (the user runs it).  Docker stays a target.
12. Docker Hub image, after a round or two of refactoring (user,
    2026-11-04): a production image (innd as news, not a test build);
    publishing needs the user's approval.

**Still C**
All INN source is C++ (2026-11-03).  C left on purpose: the tests (they
check the C/C++ boundary) and the CMake feature checks (cmake/checks/).
In progress (architect, 2026-11-04): the seven tests that include a
program's internal headers become C++, so those headers can use C++ types
(CONVERSION_NOTES.md, "Internal tests in C++").  Next: `HDRCONTENT` in
C++, then a profile of innd's feed intake (93% of its CPU is system time).
Left from the conversion:
- innfeed/ is C++ (2026-11-01).  Move the parser to bison lalr1.cc /
  flex C++ scanner classes (needs bison 3; macOS has 2.3, so ask before
  adding Homebrew bison).
- imapfeed (innfeed/imap_connection.cc): ports fixed at 143 (IMAP) and
  2003 (LMTP); configurable ports would need new innfeed.conf keys, so ask
  the user first.

**Process improvements (proposed 2026-10-30)**
- `lib/network/client` flake: the connect-timeout test got its own port
  (2026-10-30), unconfirmed as the fix.  If it fails again, investigate
  further (CONVERSION_NOTES.md, "Flaky test: lib/network/client").
- Run `support/docker-linux-checks.sh` before pushing (not per commit:
  it takes minutes).
- Add a live-harness configuration where the news user is not the tester.

**Comparative analysis, original C vs C++ (asked 2026-10-29)**
Round 1 done 2026-11-04: doc/perf/2026-11-04-alpha3.md (spec
doc/spec/performance-comparison.md, tools in support/perf/).  Next round
after the refactoring, as a new column.
Two rounds: when the conversion is complete, and after the refactoring.
Same machine, compiler and flags; original from `main` (Autoconf/Make), C++
from `cmake-build`; record method and raw numbers so rounds compare.
- Developer-written lines only (no configure, generated or bison/flex
  output), comments and blanks counted separately.
- Size of executables and libraries (stripped, unstripped, `size`).
- Test and live-test wall-clock time and peak RSS (`/usr/bin/time -l`),
  repeated, with the spread.

**Refactoring candidates (after the conversion)**
- Leaks inherited from 2.8.0 (CONVERSION_NOTES.md, "Upstream bugs and
  quirks found", 2026-11-04).  Medium priority: innfeed's
  `mainOptionsProcess` double copy and `delHost` not freeing
  `host_param_s`.  Low: makehistory, expireover and the nnrpd session.
- `ARTHANDLE::groups` (include/inn/storage.h) is `char *` though the storage
  methods only read it; make it `const char *` and drop art.cc's
  `const_cast` for `replic`.
- A feed's `R` field copies a folded Xref with its CRLF, splitting the
  feed file entry over two lines (upstream behavior; ask before changing).
- innd/art.cc `HashFeedMatch`: `else continue;` does not advance `hf`, so an
  unknown hash type would loop for ever (unreachable today).
- Retire ovdb and Berkeley DB, after a one-way ovdb → ovsqlite converter
  with no Berkeley DB dependency (CONVERSION_NOTES.md, "Retiring ovdb").
- lib fallbacks vs the C++ standard library and Boost; pread/pwrite in
  CONVERSION_NOTES.md, "Positioned file I/O".  Cite libc++/cppreference.
- ~~ARTparsebody body scan~~ done 2026-11-04 (7d07f923c): W1 CNFS user
  CPU −17%, wall −10% on Linux.  Next of that kind: ARTparseheader, same
  memchr technique, after tests (86% line, 76% branch coverage now).
- tradindexed flushes the group entry per article (CONVERSION_NOTES.md,
  same title, 2026-11-04): `tdx_data_add` msyncs up to 3 times per
  article; batch it.  Mainly macOS/BSD (MS_ASYNC is a no-op on Linux).
- dbzsync flushes only what changed (CONVERSION_NOTES.md, same title,
  2026-11-04): `putcore` msyncs the whole dbz table every
  `icdsynccount` writes, about 20% of innd's W1 intake on macOS
  (doc/perf/2026-11-04-w1-profile.md).  Track the changed range instead.
- Perl and Python filters in the CMake build (CONVERSION_NOTES.md, same
  title, 2026-11-05): DO_PERL/DO_PYTHON are hard-wired off, so innd's and
  nnrpd's filters and auth hooks are never built or tested.  Options,
  ExtUtils::Embed / FindPython3, sample-script tests in verify-cxx.sh,
  Linux image packages.
- ChaiScript filter hooks as a successor to Perl filters
  (CONVERSION_NOTES.md, same title, proposed 2026-11-05): per-article
  speed, build cost and sandboxing measured against Perl, Python, Luau
  and AngelScript first; last release v6.1.0 (2018), so pin a commit.
  Phasing out Perl filters revises "Perl and Python stay" for filters.
- uwildmat hardening (CONVERSION_NOTES.md, "uwildmat hardening",
  2026-11-04): the matcher is replaced by lib/nativemat.cc, iterative,
  with the functions renamed nativemat* (2026-11-05).  uwildmat* stay as
  deprecated wrappers.  Open: cap client pattern length, measure innd's
  feed matching, PERMmatch reverse scan, and remove the wrappers in a
  later release.
- Flaky test nnrpd/auth-ext (CONVERSION_NOTES.md, same title,
  2026-11-05): "caught signal 1" from the auth program, once; not yet
  investigated.
- Legacy C functions with standard C++ replacements (qsort, bsearch, ...):
  once the last use of one is converted, remove what remains of it from
  the source tree: leftover comparators and helpers, INN's own
  replacement implementations, their CMake checks and config.h symbols.
  qsort and bsearch have no uses left (2026-11-02; INN has no
  replacement or check to remove for either).
- Research moving innfeed's configuration parser (configfile.y/.l, bison
  and flex) to Boost.Parser (in Boost since 1.87, header-only, C++17):
  what the grammar, the include handling in the lexer and the scope/value
  tree would take, error messages and line numbers, and whether it drops
  the bison/flex build dependency.  An alternative to the lalr1.cc move
  (innfeed, under Still C); decide between them before doing either.
  Boost's config-file readers are not an option without changing the file
  format (2026-11-02): ProgramOptions is flat INI; PropertyTree's INFO
  format nests but has no `key: value` colon, uses `;` comments and
  `#include`, and keeps every value a string.  Either breaks existing
  innfeed.conf files and innfeed.conf(5).  A PropertyTree `ptree` could
  still replace the hand-rolled scope/value tree, filled by whichever
  parser reads the current format.  Decision 2026-11-02: keep the
  bison/flex parser for now.
- 49 struct/enum typedefs with bodies → `struct X { ... };` during class
  work (not clang-tidy's fix).
- Upstream bugs to fix or report: CONVERSION_NOTES.md, "Upstream bugs and
  quirks found".
- Deferred modernization (on hold by decision): 323 C-style casts, 2430
  uninitialized variables, 786 implicit bool conversions.

**Housekeeping and documentation**
- Delete the `cxx-conversion-wip` and `legacy-2020` branches once the class
  work no longer needs them (confirm with the user first; irreversible).
- Convert `readme.pod` to `README.md`.

---

General C++ style and class-design references (Effective C++, Effective
Modern C++, the canonical class): [CPP_GUIDELINES.md](CPP_GUIDELINES.md).
