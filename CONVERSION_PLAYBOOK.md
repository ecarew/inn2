# Conversion playbook: C/Autoconf to C++/CMake

A starting point for the next conversion like INN++ (C with Autoconf/Make to
C++20 with CMake only), written after doing it once (October 2026).  It
records the order of work, the gates, the tools worth carrying over, and the
mistakes that cost time.  The INN-specific record of that conversion is
[CONVERSION_NOTES.md](CONVERSION_NOTES.md); the tools named here are in
`support/` and `support/cxx-tools/` in this repository.

## Order of work

What we would do in this order next time (INN++ did 4 before 2 and 3, and
paid for it):

1. **Set up the safety net before touching code.**
   - Branches: `main` = unmodified upstream, one work branch.
   - A notes file (decisions, reasons, alternatives, commit references) and a
     CLAUDE.md (rules and open items only).  Skeletons below.
   - A verification script that builds and runs the existing tests against
     the build, and a commit wrapper that runs it and refuses to commit on
     failure (INN++: `support/verify-cxx.sh`,
     `support/cxx-tools/commit-verified.sh`).
   - Linux in Docker from day one, as part of verification, not as an
     afterthought.
2. **Make the CMake build self-contained** before converting anything:
   - Platform checks in a CMake module, generating config.h from a template
     (`cmake/InnConfigure.cmake`, `include/config.h.cmake.in`).
   - Every generator (method tables, SQL helpers, version and path headers)
     run by CMake into the build tree, with its inputs as dependencies.
   - Prove equivalence with the old build before switching: compare
     config.h symbol by symbol (`support/compare-config.sh`) and generated
     files byte for byte.
   - Nothing generated in the source tree; add a check for it.
3. **Measure coverage and write tests** per directory until it passes the
   gate (we used 33% line coverage, and every built program run by at least
   one test).  Use a coverage build (clang source-based coverage) and a
   report script.  Servers need a live harness (start the daemon, feed it,
   query it).
4. **Convert, one file per commit**, mechanically: rename, build, fix only
   what the compiler reports.  Then per-directory modernization passes
   (nullptr, void-arg, bool literals) as separate commits.
5. **Security audit** of privilege handling (setuid, root drops, writers
   refusing root), with root-path tests run under sudo and in Docker.
6. **Refactor** (classes, RAII, containers) only after the conversion is
   complete and covered.

## Gates and rules that paid off

- **Tests before conversion.**  The gate found real bugs every time: a
  privilege regression, a silent empty-history failure, configure bugs,
  upstream bugs.  None was visible until something ran the code.
- **Equivalence checks against the old build**: config.h symbol by symbol,
  generated files byte for byte, program output against the original C
  build.
- **One file per commit, verification before each, a `Tested:` section** in
  the message listing what ran on that exact tree.
- **C linkage in the headers** (`BEGIN_DECLS`/`END_DECLS`), never
  `extern "C"` blocks in sources, so converted and unconverted files keep
  linking.
- **Test programs stay C**: a standing check of the C/C++ boundary, and the
  same tests run against both builds.
- **Feature tests, not platform names**, for every platform difference.
- **Open items with a dated Resolved log**, and decisions recorded with their
  reasons in the same commit as the change.

## Mistakes to avoid

| What happened in INN++ | Do instead |
|---|---|
| Converted while CMake still read files Autoconf/Make had left in the tree; those went stale and could shadow the real headers. | Make CMake self-contained first (step 2). |
| Linux problems (libcrypt, static-library order, dbm library) surfaced late. | Build and test on Linux in every verification run. |
| A privilege-drop regression got through: the live harness made the tester the news user, which hides the "ordinary user who is not news" case. | Keep one test configuration where the service account is not the tester. |
| A flaky network test stopped three long commit series before anyone looked at it. | On the first flake, make the harness print the failing output; fix the race. |
| Generated files and leftovers in the source tree twice had to be moved aside. | Build-tree generation only, checked by verification. |
| The test-harness failure output was discarded, so intermittent failures left nothing to diagnose. | Always print a failing test's output. |
| A run test from Autoconf left a scratch file in the repository root. | Run configure-time test programs in a scratch directory of the build tree. |
| Full verification on every commit made 13-commit series take hours. | Consider: targeted checks per commit, the full suite (plus Linux and the live harness) at the end of each series before pushing.  Decide with the user. |
| CLAUDE.md grew into a history log. | Rules and open items only; history goes in the notes' conversion log. |

## Tools worth carrying over

All in this repository; adapt names and paths.

- `support/cxx-tools/commit-verified.sh`: runs verification and the live
  test, writes the `Tested:` section, commits only on success, refuses with
  unstaged changes, restores the index when it does not commit.
- `support/cxx-tools/convert-file.sh`: `git mv` to `.cc`, update
  CMakeLists.txt, rename C++ keywords (`fixkw.pl`), build, print the errors.
- `support/cxx-tools/autocast.sh` (+ `castrhs.pl`): add `static_cast` for
  the compiler's "void * to T" errors on single-line assignments.
- `support/cxx-tools/tidy-fix.sh`: one clang-tidy check's fixes on one
  directory's `.cc` files, then clang-format; stops if anything else changed.
- `support/verify-cxx.sh`: builds, checks exported symbols and C linkage,
  compiles and runs the C test programs against the C++ libraries, runs
  shell tests in a sandbox, prints failures.
- `support/live-innd.sh`: a live daemon harness with sections per directory.
- `support/coverage-report.sh`: clang source-based coverage, per file.
- `support/root-checks.sh`, `support/docker-root-checks.sh`: started-as-root
  behavior, on macOS under sudo and on Linux in Docker.
- `support/compare-config.sh`: CMake's config.h against configure's.
- `cmake/InnConfigure.cmake` and `cmake/checks/`: a model for porting
  configure.ac, including Autoconf run tests via `try_run`.

## Recommended setup (macOS, Homebrew)

Installed for INN++ on 2026-10-30, after the fact; install them first next
time.

| Tool | Use |
|---|---|
| `ccache` | Compiler cache; set `CMAKE_<LANG>_COMPILER_LAUNCHER`.  Verification rebuilds several build directories per commit. |
| `ninja` | Faster builds (`cmake -G Ninja`). |
| `coreutils` | `gtimeout`: macOS has no `timeout`, needed around servers that may hang. |
| `shellcheck` | The harnesses are shell; it catches quoting, `$?` and word-splitting bugs. |
| `include-what-you-use` | Header cleanup during refactoring. |
| `gersemi` | CMake formatter. |
| `hyperfine`, `tokei`, `cloc`, `bloaty` | Comparing the original and converted builds: timing, lines of code, binary size. |
| `krb5` (keg-only), `dovecot` | Test stand-ins: a scratch Kerberos KDC, a local IMAP/LMTP server (run in the foreground as the tester, not as a service). |
| Docker | Linux builds and root-path tests. |
| Homebrew `llvm` | clang-tidy, clang-format, clang-apply-replacements. |

## Porting configure.ac to CMake: checklist

- List config.h's symbols, then grep the sources: about a quarter were never
  used (Autoconf and libtool bookkeeping, replacement-function selectors,
  type fallbacks).  Leave those out, and list them in the comparison script
  with the reason.
- `check_include_file`, `check_function_exists`, `check_symbol_exists`
  (declarations: `#cmakedefine01`), `check_type_size`,
  `check_struct_has_member`; probe with the same feature macros config.h
  enables (`_GNU_SOURCE`, `_DARWIN_C_SOURCE`, ...).
- `check_c_source_compiles` links: give it the library.  In C a wrong pointer
  type is only a warning, so const-ness tests need `-Werror`, and `-Werror`
  then catches unrelated warnings (null arguments) too.
- Copy the project's own m4 test programs unchanged into `cmake/checks/` and
  `try_run` them in a scratch directory.
- Choose replacement functions (AC_REPLACE_FUNCS / LIBOBJS) from the checks.
- Expect configure itself to be wrong sometimes (INN's `tm_gmtoff` test
  lacked `<time.h>`); confirm in config.log before siding with CMake.
- Generated headers that derive from config.h (system.h-style) can run the
  old generator script at configure time; sed-generated copies need the same
  ordered substring replacements, verified byte for byte.

## Skeleton: CLAUDE.md

```markdown
# <Project> C++ Design Rules

Converts <project> from C/Autoconf to C++20/CMake.  Reasons and history:
CONVERSION_NOTES.md.

## Build system
- CMake minimum version, C++ standard set once, no extensions.
- CMake generates config.h and all other generated files into the build
  tree; nothing generated in the source tree.
- macOS and Linux both build and pass tests; feature tests, not platform
  names.

## Inclusion of C headers
- BEGIN_DECLS/END_DECLS in headers; no extern "C" in sources.

## Conversion workflow
- Tests first (coverage gate per directory).
- One file per commit; fix only what the compiler reports.
- Modernization passes per directory as separate commits.
- Not converted: <scripting languages, tests>.

## Testing before commit
- Verification before every commit; `Tested:` section.
- Table of build directories and how to run each.

## Notes discipline
- Decisions with reasons in CONVERSION_NOTES.md in the same commit.
- Done items move to the notes' Resolved log; history goes there too.

## Open items
- Goal list (CMake-only items), still-C files, refactoring candidates.

(General C++ style references go in a separate CPP_GUIDELINES.md, linked
from here, so they do not load into every session.)
```

## Skeleton: CONVERSION_NOTES.md

```markdown
# <Project> conversion notes
(what was decided, why, what else was considered)

## Tracing a change back to the original
## Method
## Decisions
### Build system
### Keeping C and C++ code linkable during the conversion
### Names that are C++ keywords
### Other C/C++ differences that needed code changes
### Code compiled out in this build
### Testing gate for each directory
### Security
### Not converted
## Class design approach
## Planned design (not yet implemented)
## Conversion log
### Resolved
### Upstream bugs and quirks found
### Coverage history
```
