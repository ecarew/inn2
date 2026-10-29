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
  they are done, CMake reuses `./configure`'s `config.h` and the generated
  sources from one Make pass (`dc61d2089`).
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
  `lowered`, ...).  Renames were applied to code only, never to comments or
  log strings, and checked function by function for name collisions.

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

### Code compiled out in this build

Some files are almost entirely inside a feature test that is off here
(`HAVE_OPENSSL`, `DO_PERL`, `DO_PYTHON`), so building them as C++ proves
nothing.  For nnrpd's `tls.cc`, `perl.cc` and `python.cc` each conversion
also ran a syntax-only compile (`c++ -fsyntax-only -std=c++20`) with the
feature defined and the library's headers on the include path; the commits
give the exact commands.  This found six `void *` conversions in python.cc
that the normal build could not see.  For Perl, only the CORE directory
was used (`-idirafter`): the full `perl -MExtUtils::Embed -e ccopts` adds
MacPorts' `/opt/local/include`, whose headers clash with the system ones
INN was configured against.  None of this links or runs the code; that
waits for CMake-only goal item 8 (optional features).
- `static const` objects need an initializer in C++ (`= {}`, same zero value).
- Test-only prototypes under `TESTING` need C linkage, or the C tests cannot
  link (`6a8df6188`).

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

### Article header lookup: sorted array instead of a tree (decided 2026-10-28)

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

This is analysis, not measurement.  A benchmark of both with the 76 real
header names is on the open-items list and would double as the first test of
`HeaderTable`.

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
