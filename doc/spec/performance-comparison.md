# INN++ Performance Comparison: Specification

Status: approved by the architect (2026-11-04); implemented in
`support/perf/`.  First report: `doc/perf/2026-11-04-alpha3.md`.

## 1. Purpose

Compare original INN 2.8.0 (C, Autoconf/Make) with INN++ (C++20, CMake)
on the same Mac, running the same realistic news workload.  The report
measures size, speed, memory, leaks and code coverage for the major
programs.  The comparison is designed to be rerun after each major
redesign round, so that every round adds a column and changes nothing
already measured.

## 2. Principles

P1. **Real-world workflows only.**  The workload is what a news server
    does day to day: take in a feed, serve readers, feed peers, expire.
    Unit tests and test suites are not part of it.
P2. **Same bytes in.**  Every build gets an identical input: the same
    40,000 articles, generated from a fixed seed.
P3. **Same machine and same conditions.**  One Mac, mains power, no
    other heavy load.  Builds are run alternately, so slow drift in the
    machine's state affects all of them equally.
P4. **Same configuration.**  Both builds run with the same `inn.conf`,
    storage method, overview method and feeds.
P5. **Same compiler and optimization.**  Apple clang, `-O2 -g`, on both.
    Debug and sanitizer builds are never timed.
P6. **Scratch installation as an ordinary account.**  The server runs on
    high ports in a temporary directory, like `support/live-innd.sh`.
    Nothing runs as root.
P7. **Reproducible.**  A single command rebuilds the comparison.  Every
    report records the exact commits, compiler version, macOS version and
    hardware it was run on.

## 3. Builds compared

| Column | Source | Build |
|---|---|---|
| **Baseline** | INN 2.8.0, commit `6843efceb` (where INN++ forked) | `./configure && make`, in a scratch git worktree |
| **alpha.3** | `v2.8.0-cxx.alpha.3` (`54a58d613`) | CMake, Ninja, `-DINN_RUNAS_USER/GROUP` (scratch install) |
| *Round N* | the commit at the end of each redesign round | as alpha.3 |

The baseline column never changes.  A later round adds a column and
reruns the workload on all columns, so the numbers always come from one
machine at one time (P3).  Earlier reports are kept, not overwritten.

## 4. Workload

Each run starts from an empty scratch installation:
- tradspool storage
- tradindexed overview
- dbz history

### 4.1 Input

40,000 articles, written once by a deterministic generator
(`support/perf/genarticles`, C++ `std::mt19937` with a fixed seed, so the
output is fixed by the C++ standard):
- 50 groups in 5 hierarchies, about 15% crossposted to 2–4 groups
- sizes taken from a realistic distribution: most between 1 and 4 KB,
  with a tail up to 64 KB
- headers shaped like real Usenet: Path, References chains, User-Agent,
  and so on
- the file is checksummed, and the checksum is in every report

### 4.2 Scenarios

| # | Workflow | What runs | Measured |
|---|---|---|---|
| W1 | **Feed intake** | 40,000 articles streamed to innd with TAKETHIS from a feeder process | wall time, articles/s, innd CPU and memory |
| W2 | **Duplicate offers** | the same 40,000 offered again with CHECK (all refused) | wall time, innd CPU |
| W3 | **Outgoing feed** | innd feeds a peer through innfeed; the peer is a second, accept-all scratch server | time to drain the backlog, innfeed CPU and memory |
| W4 | **Readers** | 20 concurrent reader sessions on nnrpd: GROUP, OVER over each group, ARTICLE on a sample of 5,000 | wall time, per-session memory, nnrpd CPU |
| W5 | **Daily maintenance** | the expire steps `news.daily` runs, run directly (expire, then expireover, with an expire.ctl that removes about half the articles) | wall time, CPU, memory |
| W6 | **Rebuild** | `makehistory` over the spool | wall time, CPU, memory |

W1 to W6 run in that order on one installation, as a server's day would.

### 4.3 Repetition

Each build runs the whole workload 5 times, alternating between builds.
The report gives the median and the spread (minimum to maximum).

## 5. Measures

### 5.1 Code size (static, from source)

- **Total lines of code**, without test code: everything except `tests/`,
  `support/` test scripts and the CMake feature checks.
- **Lines per major program**, counting only that program's own
  directory (no shared libraries, no helper programs):
  - innd (`innd/`)
  - nnrpd (`nnrpd/`)
  - innfeed (`innfeed/`)
  - expire and expireover (`expire/expire.*`, `expire/expireover.*`)
  - makehistory (`expire/makehistory.*`)
- **The shared libraries**, listed separately, because every program
  depends on them: `lib/` (libinn), `storage/`, `history/`.
- Counted with `cloc` (code lines, without comments or blank lines).

### 5.2 Binary size

- Size of each major program, for the stripped and the unstripped build.
- Size of the three libraries.
- The baseline's libtool wrapper scripts are skipped: the real binaries
  are measured.

### 5.3 Run measures (per scenario, per process)

- Wall time, and user and system CPU, from `rusage` (`/usr/bin/time -l`).
- **Memory:**
  - Peak resident memory of each process.
  - For the long-running servers (innd, nnrpd, innfeed), the memory
    footprint sampled during the run with `footprint`.
- Throughput where it applies: articles per second, OVER lines per
  second.

### 5.4 Leaks

- Each long-running server is checked with macOS `leaks`:
  - at the end of its scenario, while still running
  - with `MallocStackLogging` on, so each leak comes with the stack that
    allocated it
- Short-lived programs (expire, makehistory) are checked with
  `leaks --atExit`.
- The report counts leaks and leaked bytes for each program and build,
  and lists INN++ leaks that the baseline doesn't have.
- The leak runs are separate from the timed runs, because stack logging
  slows a program down.

### 5.5 Code coverage

- Both builds are instrumented, as one extra untimed pass:
  - the baseline with `-fprofile-instr-generate -fcoverage-mapping` (clang
    source-based coverage), in a second scratch worktree
  - INN++ with `INN_COVERAGE`, which uses the same flags
- That pass runs the same workload W1 to W6.  It's coverage of real
  use, not of a test suite (P1).
- Line coverage is reported for each major program directory and for
  each library.  Generated code (innfeed's lex and yacc parser) is left
  out, because the two builds generate it in different places.
- The difference between the baseline and INN++ shows two things:
  - code the conversion added or removed
  - paths the workload no longer reaches, or reaches newly

## 6. Report

`doc/perf/<date>-<column>.md`, one file per run:
- **Header:** the machine, the macOS and compiler versions, each
  column's commit, and the input checksum.
- **Tables:** code size, binary size, W1 to W6 (time, CPU, memory),
  leaks, and coverage.  Each table has one row per program or scenario
  and one column per build.  The INN++ columns add a change against the
  baseline, as %.
- **Findings:**
  - regressions over 5%
  - leaks
  - coverage changes over 5 points

  Each one gets an open item.
- The raw measurements stay local and are not kept (as already decided
  in CONVERSION_NOTES).

**Failures and leaks** (architect, 2026-11-04):

- **Failed.**  When a program behaves incorrectly and can't finish a
  scenario, that scenario's cell for that program and build reads
  **FAILED**, with no numbers.  Examples: a crash, a hang past the
  timeout, wrong replies, or articles missing afterwards.  The reason
  goes in the findings.
- **Leak footnote.**  When a scenario leaks memory in a program, as
  `leaks` reports it (section 5.4), that scenario's cell for that program
  and build carries a footnote:
  > The test could lead to damage to the platform's working environment
  > and represents a likely cybersecurity risk.
  The leak count and bytes, with the allocating stack, go in the
  findings.

Example layout, with room for later rounds:

| W1 feed intake | Baseline | alpha.3 | Δ | Round 1 | Δ |
|---|---|---|---|---|---|
| wall time (s), median | | | | | |
| articles/s | | | | | |
| innd peak RSS (MB) | | | | | |

## 7. Implementation

`support/perf/`:
- `genarticles.cc`: the generator.
- `feeder`: a small NNTP streaming client, for W1 and W2.
- `readers`: the reader load, for W4.
- `run-compare.sh`: runs the workload on each build given to it and
  prints one measurement per line (`PERF_LEAKS=1` for the leak pass,
  `PERF_COVDIR` for the coverage pass).
- `report.py`: writes the report from those measurements.

The tools are written and tested first, then the runs are made.

## 8. Not included

- **Linux.** It's a second view for later, as already decided.
- **TLS, the Perl and Python hooks, and ovdb.** They aren't built in
  INN++ yet.
- **Other storage and overview methods** (CNFS, ovsqlite).  They come in
  later rounds if a redesign touches them.

## 9. Decisions

- Major programs: innd, nnrpd, innfeed, expire/expireover and makehistory
  (architect, 2026-11-04).
- W4 uses 20 concurrent readers.
- Tools: cloc (Homebrew), and from macOS and Xcode `leaks`, `footprint`
  and `xcrun llvm-cov`/`llvm-profdata` (matched to Apple clang).
