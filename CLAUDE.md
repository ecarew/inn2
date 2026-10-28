# INN++ C++ Design Rules

This project is converting the INN 2.8.0 C codebase to C++. All new and converted
code must follow the rules below.

---

## Build system

- CMake minimum version: 4.3
- C++ standard: C++20, set per target with `target_compile_features(<tgt> PRIVATE cxx_std_20)`
  and `CXX_EXTENSIONS OFF`. Chosen because Apple Clang supports it fully; C++23 support
  in Apple's libc++ is still partial. Don't use C++23-only features.
- C headers included from C++ translation units must be wrapped in `extern "C" { … }`
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

## Testing before commit

Run the full verification set *before* every commit and record it in the
commit message as a `Tested:` section listing each check and its result.
Only list checks that actually ran on the exact tree being committed.

Run `support/verify-cxx.sh` (needs a configured `build/`) and paste its output
into the `Tested:` section. Its reference data lives in `support/verify-cxx/`.

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

**Test coverage (2026-10-28, support/coverage-report.sh)**
- innd/ is 2.0% line-covered: every file except innd.cc startup is 0%
  (art, nc, cc, rc, site, chan, newsfeeds, ...).  INN's innd tests
  (tests/innd/artparse-t.c, chan-t.c, fakeinnd) are not in verify-cxx.sh yet;
  add them before any further innd changes.
  Plan: split innd's objects (all but innd.cc) into an internal innd_core
  library that innd and the tests both link; build artparse and chan with
  tests/innd/fakeinnd.c in verify-cxx.sh; baseline them against the original
  Make-built innd objects to separate test problems from regressions.
- Even then nc.cc, cc.cc, rc.cc and site.cc stay untested: they only run in a
  live innd.  Needs a scratch-configuration innd driven over NNTP (separate
  piece of work).
- Overall 35.8% of lines: lib/ 72.5%, history/ 59.8%, storage/ 36.2%.
  0% also in storage/overdata.cc, lib/inndcomm.cc, lib/network-innbind.cc.

**Deferred modernization (on hold by decision, 2026-10-28)**
- C-style casts: 323 `cppcoreguidelines-pro-type-cstyle-cast` hits in the
  `.cc` files.  Needs case-by-case judgment (static/const/reinterpret_cast).
- Uninitialized variables: 2430 `cppcoreguidelines-init-variables` hits.
  Mostly C-style declarations; noisy, and fixes can change behavior.
- Implicit bool conversions: 786 `readability-implicit-bool-conversion`
  hits (`if (ptr)`, `while (n)`).  Style; low priority.

**Other known gaps (2026-10-28)**
- 49 struct/enum `typedef`s with bodies left by `modernize-use-using`; convert
  to `struct X { ... };` during struct/class modernization.  Do not use
  clang-tidy 23.1.4's `-fix` for this check: it corrupts anonymous struct
  typedefs (see commit 085d0741d).
- Code never compiled as C++ because its feature is off in this build:
  `innd/perl.cc`, `innd/python.cc`, `lib/perl.cc` (DO_PERL/DO_PYTHON),
  `storage/ovdb/ovdb.cc` (HAVE_BDB), `lib/canlock.cc` (libcanlock).  Also
  where `NULL` still remains in compiled-out code.
- Generated sources still C: `storage/methods.c`, `storage/ovmethods.c`,
  `storage/ovsqlite/sql-*.c`, `history/hismethods.c`,
  `history/hissqlite/hissqlite-{init,main,read}.c` (their generators: goal
  item 2).
- `lib/` replacement functions not used on macOS (`asprintf`, `inet_*`,
  `snprintf`, `strlc*`, `setenv`, `mkstemp`, `pread`, `pwrite`,
  `getaddrinfo`, `getnameinfo`, `daemon`) are still C.
- The Autoconf/Make build can no longer build libinn, libinnhist or
  libinnstorage (their Makefiles list `.c` files that are now `.cc`).  CMake
  now builds all 49 C programs (2026-10-28), but only innd, tinyleaf,
  encode/decode and convdate have been run; the rest are untested.
- Kerberos (auth_krb5) is found through `krb5-config`, which on this Mac is
  Anaconda's (`/opt/anaconda3`), matching what configure picked.
- Upstream bug, innfeed/article.c:37: `#define VALIDATE_HASH_TABLE() (void(0))`
  is C++ syntax and does not compile as C when NDEBUG is defined.  Latent,
  since NDEBUG is no longer defined; fix when innfeed is converted.
- Shell-driven tests not yet run: `overview/ovsqlite-integ`,
  `overview/overchan`, `storage/sm`, `storage/archive`, `storage/makehistory`,
  `lib/hissqlite-util`, `lib/xmalloc` (need the programs above).

**Decisions**
- 2026-10-28: INN's Perl and Python code is not converted to C++.  The Python
  in `samples/` and the embedded Perl/Python filter and auth hooks are a
  site-extension interface; the ~25,600 lines of Perl admin scripts are
  text-processing tools better left in Perl; `INN::Config` is part of INN's
  Perl interface.  They only need generating, building and installing
  (CMake-only goal items 4, 7 and 8).

**Resolved**
- (none yet)

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
