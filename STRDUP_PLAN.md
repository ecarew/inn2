# Plan: converting strdup to std::string

Status: draft for the architect's approval, 2026-11-10.

## Goal

Replace hand-managed string copies (`xstrdup`, `strdup`, `xstrndup`,
`strndup` and their matching `free`) with `std::string` that owns its
memory, so string ownership is visible in the types instead of resting on
a convention. This is the same conversion already done for innfeed's
configuration and tapes (`7a411a42f..2c698a877`) and for makehistory
(`f0a0b5ca0`), which took both programs to zero leaks.

## Where the calls are

There are 314 calls in production code (tests and `support/` excluded),
counted by grep on 2026-11-10:

| Area | Calls | Main files |
|---|---|---|
| nnrpd | 119 | perm.cc 89, post.cc 8, nnrpd.cc 8, group.cc 6 |
| lib | 46 | innconf.cc 11, confparse.cc 10, vector.cc 8 |
| innd | 37 | rc.cc 15, cc.cc 8, newsfeeds.cc 4 |
| storage | 27 | tradspool.cc 7, interface.cc 5 |
| authprogs | 19 | libinnauth.cc 7, radius.cc 6, ckpasswd.cc 6 |
| backends | 17 | buffchan.cc 5, actsync.cc 5 |
| expire | 16 | makehistory.cc 9, expire.cc 3 |
| frontends | 14 | inews.cc 5, sys2nf.cc 3 |
| innfeed | 13 | imap_connection.cc 8 |
| history, include, control | 6 | |

## The four kinds of call

Every call falls into one of these kinds. Each kind has its own fix and
its own risk.

1. **Struct fields that own a copy.** For example the `nnrpd/perm.cc`
   access and auth groups (`ret->name = xstrdup(orig->name)`), the peer
   entries in `innd/rc.cc`, and the fields of `SITE`.
   - **Fix:** the field becomes `std::string`, and the struct moves from
     `xmalloc` to `new`/`delete`, or to a value held in a container. The
     hand-written copy functions (`copy_accessgroup` and the like) become
     the struct's default copy.
   - **Watch for:** a `nullptr` field means "not set" in some places. Use
     an empty string when empty and unset mean the same thing; otherwise
     use `std::optional<std::string>`. Decide field by field, and write
     the decision in the commit message.
2. **Temporary local copies:** strings copied so they can be cut up in
   place and freed at the end of the function. For example
   `innd/rc.cc:262` and `RCCommaSplit(xstrdup(...))`.
   - **Fix:** a local `std::string`, split into further strings (or by
     index) rather than cut in place. Not `std::string_view`: a view is a
     pointer into another string's buffer, which the tracked list below
     rules out.
   - **Watch for:** early returns that skip the `free` today. These are
     leaks the conversion fixes, and the commit message should say so.
3. **Values that cross a C interface.** Strings handed to or taken from
   libraries (SASL, Perl, Python, Kerberos, `getaddrinfo`), and strings
   whose `char *` result callers `free()`.
   - **Fix:** keep `char *` exactly at that boundary, and convert the
     owner on our side.
   - **Watch for:** functions whose callers free what they return, such
     as `vector_join`, which returns `xstrdup("")`. Each one changes
     together with all of its callers, in one commit.
4. **Shared infrastructure.** `lib/innconf.cc` (`struct innconf` has 56
   `char *` fields, read across every program), `lib/confparse.cc`, and
   `lib/vector.cc` (`struct vector` holds `char **`).
   - **Fix:** this is the riskiest kind, because every program reads these
     structs. `innconf` is best converted one field at a time, behind
     accessors where needed. `struct vector` should not be converted in
     place: its callers move to `std::vector<std::string>` one by one,
     like the hashtab/tst/list replacement, and it is deleted once none
     are left.
   - **Needs the architect's decision:** whether `struct innconf` may
     change its public layout. That breaks the C API the same way the
     SMsetup/OVctl change did.

## Order

Small, self-contained programs come first, to settle the patterns.
Shared infrastructure comes last.

| Phase | Scope | Why at this point |
|---|---|---|
| 1 | The rest of expire (makehistory's remaining 9, expire.cc), backends, frontends, authprogs, and the rest of innfeed | Standalone programs, few calls each, quick to test. These settle the patterns before the larger phases. |
| 2 | innd: rc.cc, cc.cc, newsfeeds.cc, and the `char **` fields of `SITE` | `SITE` is now in a vector, so it can hold strings. The live innd suite covers this code well. |
| 3 | nnrpd: perm.cc first, then post.cc, nnrpd.cc, group.cc | The most calls. perm.cc is almost entirely kind 1, so it is mechanical but large. Its copy and free functions get deleted. |
| 4 | storage | Per-method code with a lot of handle plumbing. Measure before and after, as was done for the typed storage controls. |
| 5 | lib: confparse.cc, vector.cc callers, innconf.cc | Touches every program. Needs the architect's decision on `innconf` first. |

## How each step is done

- One file per commit, unless a function that returns an owned string
  changes together with its callers.
- Each commit message says which kind each change is, how `nullptr` was
  handled, and any leak fixed along the way.
- The `unlessEmpty()` and `pathIn()` helpers from the innfeed work cover
  the common cases where an empty string means "use the default" and
  where a path is joined onto a directory.
- Functions that take `const char *` and only read their argument keep
  their signature. Callers pass `.c_str()`. Signatures change only where
  ownership crosses the call.

## How each phase is tested

- **Every commit:** the verify suite, run through `commit-verified.sh`,
  with the tests listed in a `Tested:` trailer.
- **The affected program's live suite:** `support/live-innd.sh` for innd
  and nnrpd. For innfeed and the backends, the backlog checks used for
  the tape work.
- **A macOS `leaks` pass on the matching workloads** (W1, W3, W6).
  - The expected result: the leak count stays the same or falls.
  - The known libperl leak in innd (1 leak, 1536 B) is ignored. It is out
    of scope by the architect's decision.
- **The Linux checks** (`support/docker-linux-checks.sh -c`) at the end of
  each phase, before the phase is pushed.
- **Phases 3 and 4:** also a performance comparison against the frozen
  baseline (`support/perf/run-compare.sh`). No regression in throughput
  or peak RSS is allowed.

## When it is done

- No `xstrdup`, `strdup`, `xstrndup` or `strndup` is left outside the C
  boundaries of kind 3, and each one that remains has a comment saying
  why.
- Leak counts on W1, W3 and W6 are no higher than now.
- CONVERSION_NOTES records each phase with its commits and measurements.
- `xstrdup` and `xstrndup` leave `include/inn/xmalloc.h` once nothing
  calls them. Whether to remove them is the architect's call at that
  point.

## Raw pointers to and into std::string (tracked)

The architect asked (2026-11-11) that no raw pointer to a `std::string`
object, or into its buffer through `data()`/`c_str()`, be left behind by
the conversions. A pointer passed to a call that does not keep it
(`printf`, `QIOopen(path.c_str())`, `chdir`) is not tracked. A pointer that
is stored, in a variable, a struct or a table, is.

Pointers to string objects (`std::string *`, `&s` kept): none in the
tree (grep for `string *\*` and stored address-of, 2026-11-11).

Pointers into string buffers that are stored, as of 2026-11-11:

| Where | What is stored | Lifetime risk | Plan |
|---|---|---|---|
| innd/article.h `WordList` | `words_` point into `text_` | resizing `text_` would dangle them; `start()` clears `words_` first | innd pass |
| innd/art.cc | `hp[].p`, `arth.path`, `arth.groups`, `p` cursors into the header cache and `xref`/`lines`/`keywords` | per article; the cache is not touched meanwhile | innd pass |
| innd/keywords.cc | `text` cursor into `orig_text` | local | innd pass |
| storage/tradindexed/tdx-group.cc | `name` points into a map entry's name | local; map unchanged meanwhile | storage pass |
| contrib/respool.cc, contrib/expirectl.cc | local cursors into the article and line | local | contrib pass |

Kept, with the architect's agreement (2026-11-11):

| Where | What is stored | Why it stays |
|---|---|---|
| innfeed/imap_connection.cc `imap_readCB`, `lmtp_readCB` | `str = line.data()` cursor over the response line | local to the call; `line` is not changed while `str` is in use and `str` is reset after each `GetLine`; removing it means rewriting the IMAP and LMTP reply parsers and the `char *` helpers they call |
| innfeed/imap_connection.cc `getsimple` (SASL callback) | `unlessEmpty` hands SASL `c_str()` of the `deliver_*` globals | the Cyrus SASL API takes a `const char *` result; the globals live for the process |
| authprogs/ckpasswd.cc PAM conversation | `appdata` points at the password's buffer | the PAM API takes a `void *`; scoped to the `auth_pam` call |

Each pass replaces the stored pointer with the string itself, an index or
offset into it, or a copy (not a `std::string_view`, which is the same
pointer under another name). Rows leave this table when fixed, with the
commit hash.

Fixed:

- frontends/inews.cc, the header table pointed into four static strings
  (and into the article and optarg): the table owns `std::optional<
  std::string>` bodies, 76475a79b.
- frontends/sys2nf.cc, cursors into the entry strings: each entry is
  split into strings, 90e0f3bbd.
- backends/archive.cc, a `cvector` into the Xref copy: split into a
  `std::vector<std::string>`, 7d323366a.
- innfeed/article.cc, `fname` and `msgid` were `char *` copies; the
  article holds strings and `artFileName`/`artMsgId` return references,
  3dae6e35c.
- innfeed/connection.cc `ipName`, imap_connection.cc `saslmechs`,
  `ServerName`, `mailfrom_name`, misc.cc's d_printf host name: strings,
  058e09c0e, 02ceb52a3, d8f249d75.
- innfeed/main.cc globals `InputFile` and `pidFile` pointed at
  `c_str()` of file-scope strings: `std::optional<std::string>` and
  `std::string`, 92cf0cd94.
- innfeed/host.cc `hostPeerName` returned `c_str()` of host params and
  callers kept it: returns `const std::string &`, locals are copies,
  939961c28.
- innfeed/host.cc `hostBindAddr`, `hostBindAddr6`, `hostUsername`,
  `hostPassword` and tape.cc `getTapeDirectory`: return `const
  std::string &`, cc197fe6d.

## Out of scope

- `malloc`, `calloc` and `realloc` of things other than strings, `struct
  buffer`, and `unique_ptr` for owned handles. These are follow-up plans.
- innd's libperl leak.

## Open questions for the architect

1. May `struct innconf` change its public layout (phase 5)?

Answered 2026-11-10:

2. **"Unset" versus empty.** An empty `std::string` stands for "unset"
   wherever an empty value is never valid. That covers nearly every field.
   Where C code needs `nullptr` as a signal, the boundary passes
   `s.empty() ? nullptr : s.c_str()`. `std::optional<std::string>` is used
   only where empty and unset can be shown to behave differently today.
   Each such field is named in its commit message, and any doubtful case
   goes to the architect first.
3. **Pushes.** Phase 1 goes out as one push.
