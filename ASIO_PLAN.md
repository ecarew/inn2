# Plan: moving INN++ to the Boost.Asio event-loop model

Status: approved by the architect, 2026-11-12 (all five decisions
below, as proposed).

## Goal

The architect's rule (2026-11-10): if buffchan on Boost.Asio shows no
slowdown, move the system to the Asio event-loop model. buffchan showed
none (below), so this plan covers the rest: one `io_context` per process
in place of each hand-written `select()` loop, its timer list and its
signal masking.

What that buys:

- No `FD_SETSIZE` ceiling. innd and innfeed today cannot watch a
  descriptor numbered 1024 or more; epoll and kqueue have no such limit.
- Timers, signals and I/O in one place, instead of a timeout computed
  for each `select()` call and signals masked everywhere except inside it
  (`lib/xsignal.cc`).
- Handles owned by RAII objects (`steady_timer`, `stream_descriptor`,
  `signal_set`) instead of descriptor numbers and callback tables.

Boost 1.92 is already required, and Asio is header-only, so no new
library dependency. A program that uses a thread also links
`Threads::Threads`.

## Done: buffchan (the trial)

`backends/buffchan.cc`, `daf8dfc3b`:

- One `io_context`: the read of standard input, a `steady_timer` per
  site for `-L`/`-C`, and a `signal_set`.
- A test written first against the stdio version (`tests/backends/
  buffchan.t`, 32 checks, then 35). It found an inherited bug: a line of
  32 KiB or more stopped the program (`26021e8eb`).
- Old vs new on a 500,000-line feed: byte-identical files and stderr for
  seven option sets, each with pipe and regular-file input.
- Throughput, 2,000,000 lines, best of 5, two runs: −1.7% and +0.1%.

Lessons that apply to every later phase:

1. **Some descriptors cannot be waited on.** epoll refuses regular files.
   macOS's kqueue never reports end of file on a named FIFO or a
   terminal. buffchan reads those with a thread that posts each chunk to
   the loop and waits on a semaphore until it is handled. Pipes and
   sockets, which is what innd, innfeed and the servers use, are fine.
2. **Copy-per-line costs show up.** The first version was 1–2% slower
   until the per-line string was reused. Measure before calling a phase
   done.
3. **Behaviour that was only lazy becomes prompt.** buffchan's time
   limits now fire on an idle site, and a signal no longer waits for the
   next read. Each phase lists such changes for the architect.

## Where the loops are

Counted 2026-11-11, production code only:

| Program | Loop | Size | Kind |
|---|---|---|---|
| innd | `innd/chan.cc` `CHANreadloop`: `select()` over all channels, sleeping channels, the `timer` summary; `proc.cc` SIGCHLD; `xsignal` masking | 1,360 lines; 18 `CHANcreate`, ~70 `RCHAN`/`WCHAN`/`SCHAN` calls | the server's core |
| innfeed | `innfeed/endpoint.cc` `Run`: `select()`, a timer queue, work callbacks, signals | 1,537 lines; ~110 calls of `prepareRead`/`prepareWrite`/`prepareSleep`/... | already callback-shaped |
| ovsqlite-server | `storage/ovsqlite/ovsqlite-server.cc`: `select()` over clients | one loop | multi-client server |
| ovdb_server | `frontends/ovdb_server.cc`: `select()` over clients, and the listener | two loops | multi-client server |
| nnrpd | `nnrpd/nnrpd.cc`: the daemon-mode `accept` loop (pre-forked or forking); per client, blocking reads with a timeout in `line.cc` (and TLS) | a process per client | |
| one-connection clients | `innxmit`, `nntpget`, `innxbatch`, `lib/nntp.cc`, `lib/inndcomm.cc`, `radius`, `nnrpd/auth-ext.cc`, `lib/network.cc` (connect timeout), `lib/dbz.cc` | one `select()` each, as a read timeout | not event loops |

## Design rules for every phase

1. **One `io_context` per process, one thread.** No strands and no
   locking. The only extra threads are readers for descriptors that
   cannot be waited on (lesson 1).
2. **Reactor style first.** Wait for readiness with `async_wait
   (wait_read)` / `async_wait(wait_write)` on a `stream_descriptor` and
   keep each program's own read and write code. This replaces the
   `select()` loop without rewriting buffer ownership. Moving to
   `async_read_some`/`async_write` (proactor style) is a later, separate
   step where it pays.
3. **Keep the program's own API at first.** innd keeps `CHANcreate`,
   `RCHANadd`, `SCHANadd` and the rest; innfeed keeps `prepareRead`,
   `prepareSleep` and the rest. Only their insides change, so callers and
   behaviour stay put. Typed replacements for the callbacks come after,
   one commit at a time.
4. **Signals through `signal_set`.** This replaces `xsignal` masking.
   `SIGCHLD`, `SIGHUP` (reload) and `SIGTERM` become handlers on the
   loop, so a handler never runs in the middle of other work.
5. **Timers through `steady_timer`.** Not wall-clock: a clock change
   does not fire or delay them.
6. **Line input.** When a program's own read code is replaced (rule 2's
   later step, and phase 6, which replaces it from the start), and the
   input is always a socket or a pipe, read lines with `async_read_until` on a dynamic buffer (it keeps its search
   position and enforces a size limit). Keep a custom reader only where
   input may be a regular file, a FIFO or a terminal (buffchan's case,
   lesson 1), or where a too-long line must be skipped and reading go on.
7. **Ownership in the types.** Descriptors in `stream_descriptor`,
   timers as members of the object they belong to, no stored pointers
   into strings (the rule in `STRDUP_PLAN.md`).
8. **Same method as buffchan.** Tests first, on the old code. Old vs new
   output compared. A performance bar of no slowdown, measured with
   `support/perf/run-compare.sh` where the program is in its scenarios
   and a targeted benchmark where not. Leak counts must not rise. Linux
   checks (epoll; ASan build) before any push.

## Phases

Ordered by risk, smallest first, so that each phase is proven before the
next leans on it.

### Phase 1: shared pieces in lib/

- `include/inn/asio.h`, `lib/asio.cc`: buffchan's standard-input line
  reader (QIO's line rules, the thread fallback) as a class, so innd's
  and innfeed's tests and buffchan share one copy; a helper that makes a
  `signal_set` for a list of signals with "second signal kills" as an
  option.
- libinn links `Boost::headers`.
- buffchan switches to the library reader; its test must still pass.

Small. One push with phase 2.

### Phase 2: ovsqlite-server and ovdb_server

Multi-client servers on a local socket, each with one `select()` loop.
They are the smallest real servers and have tests
(`tests/overview/ovsqlite-integ.t`, the ovdb tests when built).

- Tests first: many clients at once, a client that disconnects mid-
  request, the server's idle timeout, shutdown by signal.
- Listener as an `asio::local::stream_protocol::acceptor` (ovsqlite also
  on TCP where configured), one connection object per client.
- Perf: W2 and W5 use overview; plus a many-reader benchmark against the
  server directly.

Revised 2026-11-12, decided by the architect:

- ovdb_server is deferred.  The CMake build never enables Berkeley DB
  (nothing finds the library, so HAVE_BDB is always off and
  `frontends/ovdb_server.cc` builds as a stub), so a converted server
  could be neither compiled nor tested.  It moves to the phase after
  Berkeley DB detection is added to CMake, which is its own task.
- ovsqlite-server has no idle timeout; its one timer commits the open
  transaction (`transtimelimit`).  The tests check that timer instead
  (`tests/overview/ovsqlite-server-t.cc`, 23 checks, committed before
  the conversion).
- Performance bar, decided by the architect: Linux (production) must
  show no slowdown; a macOS slowdown is accepted and recorded.  The
  reactor ovsqlite-server, 32 clients x 5000 get_artinfo (its cheapest
  request) plus 20,000 add_article, old and new alternated: Linux
  reads +1.5%, writes +1%; macOS reads -7%, writes +6%.  The macOS cost
  is Asio's kqueue reactor calling kevent() each time a read wait is
  armed (epoll registers once).  Not cured by the unsafe concurrency
  hint (~1%), a speculative next read (worse), or
  BOOST_ASIO_DISABLE_KQUEUE (no better).  The proactor style
  (`async_read` of the length, then the body, with no readiness wait
  first) is the follow-up if the macOS figure ever matters.

### Phase 3: innfeed

`endpoint.cc` is already an event-loop library behind a callback API,
so this is mostly one file.

- Tests first: the existing live tests (innfeed to tinyleaf, imapfeed
  against dovecot), tapetest.sh, plus endpoint unit tests: timer order,
  `updateSleep`/`removeTimeout`, a work callback, a read and a write
  completing, the priority order of ready endpoints.
- `Run()` keeps its per-pass structure but waits in the io_context
  (`run_one()` then `poll()`). Each `EndPoint` holds a
  `stream_descriptor`, closed on `delEndPoint`, as the old code closed
  the descriptor (correcting this plan's earlier "released, not
  closed"; the architect agreed to keep the close). Regular files
  cannot be waited on and are treated as always ready.
- Decisions, approved by the architect:
  1. Ready endpoints are served in the order they became ready;
     `reorderPriorityList`, `selectHits` and `SELECT_RATIO` go, as
     nothing depended on the main endpoint going first.
  2. The sorted timer queue stays; one `steady_timer` is armed at its
     head, rather than one timer per `TimeoutId`.
  3. Exception (out-of-band) waits are dropped; nothing asked for them.
- Signals: `setSigHandler` uses an `asio::signal_set`.
- Done in 148fa615e. Tests: `tests/innfeed/endpoint-t.cc` (35 checks,
  which also found and fixed a doRead offset bug, 19e2506dd) and the
  live checks. Perf, W3 on macOS, 5 alternating runs: 12.86 s new vs
  13.01 s old, innfeed user/sys unchanged; no innfeed leaks.

### Phase 4: innd

The core, done last and in steps, each pushed only after a full perf
run.

1. Tests first: innd's channel test (`tests/innd/chan`), the live tests,
   plus new ones for sleeping channels, a channel that fills its write
   buffer, `ctlinnd` while a feed is busy, `SIGHUP` reload and child
   exits (`proc.cc`).
2. `CHANreadloop` on an `io_context`, keeping the `CHANNEL` API: each
   channel holds a `stream_descriptor`; `RCHANadd`/`WCHANadd` start an
   `async_wait`; `SCHANadd` arms the channel's `steady_timer`; the
   `timer` summary becomes a timer of its own.
3. Signals: `SIGCHLD` (`PROCcatchsignal`), `SIGHUP`, `SIGTERM`,
   `SIGUSR1` (`CCresetup`) move to a `signal_set`, and `lib/xsignal`'s
   masking goes once nothing uses it.
4. Fairness: the `select()` loop serves at most one read per channel per
   pass, and some code depends on how long a pass takes (the comment at
   `chan.cc` line ~815). Measure before and after with W1 and keep the
   per-pass limits as per-handler limits.
5. The Perl and Python filters stay synchronous on the loop thread, as
   today: a slow filter still blocks innd, no worse than now.

Perf: W1–W6, alternating old and new as run-compare does, plus innd's
peak RSS and the `timer` summary's numbers.

**Done** (5941c7abc and 6f0856157 tests, ca128aea4 verify check,
b8c26afee conversion). Decisions approved by the architect:

1. Fairness unchanged: the per-pass structure stays; async waits only
   mark descriptors ready, and each pass serves the control channels
   first, then at most one read and one write per channel, round-robin.
2. The wait ends at the earliest sleeping channel's wake time (it used
   to wait up to `TimeOut`, 300 s, on a quiet server).
3. The timer summary and history/active sync stay checks at the start of
   a pass, with the wait cut short for them.
4. `CHANdiagnose` is removed.

Findings: Asio state lives in a table indexed by descriptor, because
`CHANNEL` is copied wholesale; `SITEspool` swaps a channel's descriptor
in place, so every close drops the registration first (`CHANdropfd`).
The tests found that an idle innd ignored SIGTERM/SIGHUP until other
activity (an interrupted `select()` went back to waiting); fixed by the
`signal_set`. Signals no longer need masking in innd; `lib/xsignal`
itself goes in the lib-retirement phase.

Perf on macOS, 5 alternating rounds (accepted by the architect: a small
cost for supportable code): W1 3339 vs 3555 articles/s (−6%), innd CPU
11.40 vs 11.07 s; W3 13.81 vs 13.23 s (−4%); W2, W4, W5, W6 unchanged
or faster. Leaks: none new (innd's single 1536-byte libperl leak is out
of scope). Linux figures from the Linux checks.

### Phase 5: nnrpd's listener

Convert the daemon-mode listener (`-D`): the `accept` loop in both the
forking and the pre-forked (`-P`) modes, about 40 lines each. The
per-client code is phase 6.

- Tests first: connections accepted on every listening socket, a
  pre-forked child replaced when it exits, shutdown by signal.
- Small and low-risk: it removes nnrpd's last `select()` loop and its
  `FD_SETSIZE` limit, and gives the forking parent a clean signal path.

**Done** (d33651867 tests, f0f55f03e conversion). A `Listener` in
`nnrpd.cc` waits on the listening sockets (borrowed, never closed) and
accepts one connection at a time. Decisions:

1. Listening sockets are non-blocking, so a pre-forked child that loses
   the race for a connection goes back to waiting; accepted sockets are
   made blocking again (macOS inherits the listener's `O_NONBLOCK`).
2. Fork on demand: the listener's `signal_set` handles `SIGCHLD` and
   `SIGHUP`, with `notify_fork` around each `fork()`; the child drops
   the listener.
3. Pre-forked: the parent only forks and waits, unchanged; each child
   builds its own listener after the fork (a kqueue does not survive
   one).

Perf on macOS, 5 alternating rounds: W1 3462 vs 3480 articles/s; W4
15.93 vs 15.58 s, nnrpd sys 168.8 vs 160.3 s. Leaks unchanged (nnrpd
session 31 in both). Linux figures from the Linux checks.

The 31 session leaks belong to macOS, not INN: every block that could be
traced was allocated by system libraries (libnotify's tables, set up by
`openlog()`), and macOS's fork handlers reset that state in the child,
orphaning the parent's copies. The same session run inetd style (no
fork) has 0 leaks, and a 20-line C program that only calls `openlog`,
`syslog` and `getaddrinfo` before `fork()` shows 33 leaks in its child.
Left alone by the architect's decision.

### Phase 6: nnrpd's per-client I/O on `ssl::stream` (optional)

Each nnrpd process serves one client, so an event loop adds no
concurrency there. The reason to convert is TLS.

**Today.** nnrpd calls OpenSSL directly. `SSL_read` can leave decrypted
data buffered inside OpenSSL, where `select()` on the socket cannot see
it, so a readiness wait could block for data that has already arrived
(the comment at `nnrpd/line.cc` line ~124). The TLS path therefore skips
`select()` and does a blocking `SSL_read` inside `alarm()` for the idle
timeout (line ~145). The costs: two read paths (plain and TLS), a
timeout that depends on a signal interrupting a blocking call, and no
idle-time statistics for TLS clients.

**With Asio.** `asio::ssl::stream<tcp::socket>` calls `SSL_read` first
and waits on the socket only when OpenSSL asks for more input, and does
any write OpenSSL needs in the middle of a read (renegotiation, key
update) within the same operation, so the buffering problem cannot
happen. `async_read_until(stream, dynamic_buffer, "\r\n", ...)` reads
command lines the same way with or without TLS.

- One read path for plain and TLS connections.
- The idle timeout is a `steady_timer` that cancels the read: no
  `alarm()`, and idle statistics for TLS clients too.
- The TLS handshake and shutdown are ordinary operations with timeouts.
- A small `io_context` per nnrpd process: Asio's blocking operations
  have no timeout, so even one connection needs the loop for its timer.

**What stays.** The TLS policy: `asio::ssl::context::native_handle()`
is the real `SSL_CTX`, so most of `nnrpd/tls.cc` (certificates,
ciphers, session cache, client-certificate checks) is kept. Asio
replaces the reading and writing, not the configuration.

**What to watch.**

- **STARTTLS command injection.** Whatever plaintext is already in the
  read buffer when STARTTLS is accepted must be thrown away before the
  handshake, or commands sent before TLS would be taken as sent over it.
  Today `line_reset(&NNTPline)` does this (`nnrpd/misc.cc` line ~651);
  with `read_until` it becomes clearing the dynamic buffer before the
  socket is wrapped in `ssl::stream`.
- **Layers above TLS.** `COMPRESS DEFLATE` and SASL security layers sit
  above TLS today and must sit above the `ssl::stream` the same way.
- **Writes.** Every write path moves, including `SSL_writev` in
  `nnrpd/article.cc`.

**Tests first**, on the current code: STARTTLS with pipelined plaintext
after it (must be discarded), an idle timeout on a TLS connection and on
a plain one, a TLS record split across reads, compression over TLS,
SASL with a security layer, and a client that drops mid-handshake.
Perf: the reader scenarios in `support/perf` (`readers.cc`), old vs
new. `readers.cc` speaks plain NNTP only today; a TLS mode is added to
it first, so TLS is measured too.

### Not converted: one-connection clients

`innxmit`, `nntpget`, `innxbatch`, `lib/nntp.cc`, `lib/inndcomm.cc`,
`radius`, `auth-ext`, `lib/network.cc` and `lib/dbz.cc` use `select()`
only as a timeout on one blocking read, write or connect. An event loop
adds nothing there. Proposal: replace those `select()` calls with
`poll()` so they too lose the `FD_SETSIZE` limit, as a small separate
cleanup, and otherwise leave them.

## Risks

- **innd's timing.** Its loop's fairness rules are implicit. Phase 4
  step 4 exists for this, and every innd step is measured on its own.
- **Platform quirks.** kqueue's FIFO/terminal gap (handled); the Linux
  checks are the only epoll coverage, so they run before every push.
- **Build time.** Asio headers are heavy. Measure a clean build before
  and after phase 1; if it grows much, use one translation unit per
  program for Asio code and keep headers out of shared ones.
- **Two loops at once.** A program is never half-converted in a push:
  each phase moves a whole program's loop.

## Decisions (the architect, 2026-11-12)

All five approved as proposed:

1. **Phase order:** shared pieces in lib/, the overview servers,
   innfeed, innd, nnrpd's listener, nnrpd's per-client I/O.
2. **nnrpd:** the listener (phase 5) is in the plan; phase 6
   (per-client I/O on `ssl::stream`) is an optional last phase.
3. **One-connection clients:** `select()` becomes `poll()`, as a small
   separate cleanup; no event loop.
4. **Style:** reactor first, keeping each program's own read and write
   code; proactor (`async_read_some`, `async_read_until`) later, where it
   pays.
5. **Pushes:** one per phase, phases 1 and 2 together.
