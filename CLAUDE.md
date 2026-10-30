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
  which stay C (new tests are written in C too).

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
Items 1 (native configuration), 2 (generated sources) and 9 (CTest:
`ctest --test-dir build`, tests/CMakeLists.txt) are done.
3. Generated configuration and helpers: `samples/*` (inn.conf etc. via
   fixconfig) and `support/fixconfig`, `support/fixscript`.  inn.conf is
   generated by CMake (samples/CMakeLists.txt, 2026-10-30); the other
   samples are not yet.
4. Scripts: generate the shell and Perl programs from their `.in` files.
5. Installation: install rules with INN's ownership, setuid and modes; then
   audit them (innbind must stay the only setuid-root file).
6. Man pages from `doc/pod`.
7. Perl module `perl/INN/` (INN::Config, ovsqlite_client) and `contrib/`.
8. Optional features as CMake options: Perl and Python hooks, Berkeley DB
   (ovdb), OpenSSL/TLS, libcanlock, systemd, blocklist.  Before ovdb can be
   on, fix `storage/ovdb/ovdb.cc` (fails a `-DHAVE_BDB` syntax check).
10. Portability: a BSD.  Linux runs everything (verify-cxx.sh,
    live-innd.sh, root checks) via `support/docker-linux-checks.sh` and
    `support/docker-root-checks.sh` (2026-10-30).

**Still C**
- innfeed/ (10 files; deferred).  Open: after a duplicate reply from
  tinyleaf innfeed sleeps the connection and spools the rest; replaying a
  backlog with `-x` offers nothing.  imapfeed needs an IMAP/LMTP stand-in.
  Coverage not measured.  Convert its parser to bison lalr1.cc / flex C++
  output at the same time.  Fix `VALIDATE_HASH_TABLE()` in article.c:37
  (C++ syntax, breaks C builds with NDEBUG).
- authprogs/auth_krb5.c: needs a Kerberos KDC to test.
- frontends/feedone.c, sys2nf.c: not built by CMake (OPTIONAL in Make).
- Code compiled out in this build (`innd/perl.cc`, `innd/python.cc`,
  `lib/perl.cc`, `lib/canlock.cc`): run the `-fsyntax-only` check with the
  feature on, as was done for nnrpd's (CONVERSION_NOTES.md, "Code compiled
  out in this build").

**Process improvements (proposed 2026-10-30)**
- `lib/network/client` flake: the connect-timeout test got its own port
  (2026-10-30), unconfirmed as the fix.  If it fails again, investigate
  further (CONVERSION_NOTES.md, "Flaky test: lib/network/client").
- Run `support/docker-linux-checks.sh` before pushing (not per commit:
  it takes minutes).
- Add a live-harness configuration where the news user is not the tester.

**Comparative analysis, original C vs C++ (asked 2026-10-29)**
Two rounds: when the conversion is complete, and after the refactoring.
Same machine, compiler and flags; original from `main` (Autoconf/Make), C++
from `cmake-build`; record method and raw numbers so rounds compare.
- Developer-written lines only (no configure, generated or bison/flex
  output), comments and blanks counted separately.
- Size of executables and libraries (stripped, unstripped, `size`).
- Test and live-test wall-clock time and peak RSS (`/usr/bin/time -l`),
  repeated, with the spread.

**Refactoring candidates (after the conversion)**
- Retire ovdb and Berkeley DB, after a one-way ovdb → ovsqlite converter
  with no Berkeley DB dependency (CONVERSION_NOTES.md, "Retiring ovdb").
- lib fallbacks vs the C++ standard library and Boost; pread/pwrite in
  CONVERSION_NOTES.md, "Positioned file I/O".  Cite libc++/cppreference.
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
