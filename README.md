# INN++

INN++ is the InterNetNews news server converted from C to C++.  It starts
from [INN](https://github.com/InterNetNews/inn) 2.8.0, the development
version on the InterNetNews `main` branch (commit `6843efceb`), and
modernizes it in two ways: the source compiles with any C++20 (or later)
compiler, and the GNU Autoconf and Make build is replaced by CMake.

INN++ is an independent project, not a release of the InterNetNews project
(see [Who's Responsible](#whos-responsible--who-to-thank)).  It is under
active development and may or may not be stable at any given time; build
from a tagged release (see [Download](#download)).  Contributions and bug
reports are welcome and encouraged.

## Conversion goals

- **Same server.**  INN++ behaves as INN does: the same programs, the same
  NNTP protocol support, and the same configuration and data files, so an
  INN installation can move to INN++ without changes.  Upstream bugs found
  along the way are fixed or recorded, not silently changed.  The one
  deliberate exception is UUCP, now a historical footnote: the UUCP-only
  parts (send-uucp, the 7-bit encode/decode batch tools and c7unbatch,
  setuid rnews for the uucp group, and the uux/uustat settings) are taken
  out.  What current processing still needs stays, notably **rnews** and
  the `#! rnews` batch format with gzip and bzip2 batches.
- **C++20.**  Every program and library is compiled as C++20.  The standard
  library comes first, then Boost (header-only), then INN's own code;
  legacy C functions with standard C++ replacements are removed once
  nothing uses them.
- **CMake only.**  CMake 4.3 or later builds, tests and installs INN++, and
  generates everything `configure` used to.  Autoconf and Make are removed
  once CMake covers all of it.
- **Portable by feature tests.**  macOS and Linux both build and pass the
  tests; platform differences are chosen by feature tests, never by
  platform name.  A BSD is next.
- **Tested before it changes.**  A directory is converted only once its
  programs are run by tests and it reaches a minimum line coverage, and
  every commit is checked against the test suite and a live news server
  test.  The tests stay in C.
- **Secure by default.**  No INN++ program runs as root in production
  except for a brief start-up before dropping privileges; innbind is the
  only setuid-root file.  Memory errors found during the conversion
  (buffer overflows, double frees) are fixed.
- **Hooks unchanged.**  INN's Perl and Python filters and authentication
  hooks, and the Perl and shell scripts, keep working as they are; they are
  not converted.
- **Then refactor.**  Once the conversion is complete, the code moves
  towards classes and modern C++ in separate, tested steps.

You'll also find very useful information about the original INN on [Russ
Allbery's web site](https://www.eyrie.org/~eagle/software/inn/).

Please see:

- [CHECKLIST](https://www.eyrie.org/~eagle/software/inn/docs/checklist.html)
  for a quick overview of all the installation steps and pointers to
  documentation,
- [INSTALL](https://www.eyrie.org/~eagle/software/inn/docs/install.html)
  for full installation instructions,
- [FAQ](https://www.eyrie.org/~eagle/faqs/inn.html) for INN FAQ,
- [NEWS](https://www.eyrie.org/~eagle/software/inn/docs/news.html) for
  what's changed from the previous release,
- and
  [LICENSE](https://www.eyrie.org/~eagle/software/inn/docs/license.html)
  for the copyright, license, and distribution terms.

These files can also be found in the `doc` or `doc/pod` directories if not
present at top-level.

# What is INN++?

INN++ is INN, described below, converted to C++20 and built with CMake.
It is the same news server: the same programs, protocols, configuration
files and filter hooks, so what follows about INN holds for INN++ too.
What INN++ changes, and why, is under [Conversion goals](#conversion-goals)
above.

INN (InterNetNews), originally written by Rich Salz, is a very full-featured
and extremely flexible and configurable Usenet / Netnews news server.  For a
complete description of the protocols behind Usenet and Netnews, see RFC
3977 (NNTP), RFC 4642 updated by RFC 8143 (TLS/NNTP), RFC 4643 (NNTP
authentication), RFC 4644 (streaming NNTP feeds), RFC 5536 (USEFOR),
RFC 5537 (USEPRO), RFC 6048 (NNTP LIST additions), RFC 8054 (NNTP
compression) and RFC 8315 (Cancel-Lock) or their replacements.

In brief, Netnews is a set of protocols for exchanging messages between
a decentralized network of news servers.  News articles are organized
into newsgroups, which are themselves organized into hierarchies.
Each individual news server stores locally all articles it has received
for a given newsgroup, making access to stored articles extremely fast.
Netnews does not require any central server; instead, each news server
passes along articles it receives to all of the news servers it peers
with, those servers pass the articles along to their peers, and so on,
resulting in "flood fill" propagation of news articles.

A news server performs three basic functions: it accepts articles from
other servers and stores them on disk, sends articles it has received out
to other servers, and offers stored news articles to readers on demand.
It additionally has to perform some periodic maintenance tasks, such as
deleting older articles to make room for new ones.

Originally, a news server would just store all of the news articles it had
received in a file system.  Users could then read news by reading the
article files on disk (or more commonly using news reading software that
did this efficiently).  These days, news servers are almost always
stand-alone systems and news reading is supported via network connections.
A user who wants to read a newsgroup opens that newsgroup in their
newsreader software, which opens a network connection to the news server
and sends requests for articles and related information.  The protocol
that a newsreader uses to talk to a news server and that a news server
uses to talk to another news server over TCP/IP is called NNTP (Network
News Transport Protocol).

INN accepts articles over NNTP connections.  **innd**, the heart of INN,
handles NNTP feeding connections directly; batches of articles from other
sources are handed to **innd** by **rnews** (included in INN).  Other parts of INN handle feeding articles out to other news
servers, most commonly **innfeed** (for real-time outgoing feeds) or
**nntpsend** and **innxmit** (used to send batches of news created by **innd**
to a remote site via TCP/IP).  (The original INN also carried news over
UUCP; INN++ does not.)

The part of INN that handles connections from newsreaders is **nnrpd**.

Also included in INN are a wide variety of supporting programs to handle
periodic maintenance and recovery from crashes, process special control
messages, maintain the list of active newsgroups, and generate and record
a staggering variety of statistics and summary information on the usage
and performance of the server.

INN also supports an extremely powerful filtering system that allows the
server administrator to reject unwanted articles (such as spam and other
abuses of Usenet).

INN is free software, supported by Internet Systems Consortium and
volunteers around the world.  See
[Supporting the INN Effort](#supporting-the-inn-effort) below.

# Prerequisites

Compiling INN++ requires a C++20 compiler.  It is built and tested with
**clang** (Apple clang 21 on macOS) and **gcc** (GCC 14 on Debian trixie);
any compiler with good C++20 support should work.  The Boost libraries,
1.92 or later, are also required, but only their headers: INN++ links
against no Boost library.  Compiling INN++ also requires **bison** and
**flex** for the **innfeed** configuration parser.

INN++ is built with **CMake** 4.3 or later, which probes the capabilities
of your system in place of the GNU **autoconf** `configure` script of the
original INN.  A typical build is:

    cmake -S . -B build
    cmake --build build
    ctest --test-dir build

**CMake** can drive **make**, **ninja** or another build tool of your choice
(`cmake -G Ninja`).  INN++ makes extensive use of mmap(), which can cause
problems on some older operating systems.  If you encounter problems
compiling or running INN++, or if you successfully run it on a platform
other than macOS and Linux, please let us know (see
[Reporting Bugs](#reporting-bugs) below).

Perl 5.8.0 or later is required to build INN++ (the build runs Perl
scripts) and to run several subsystems.  The embedded Perl filtering and
authentication hook support is highly recommended (some excellent spam
filters have been written for INN); it needs Perl's embedding support,
ExtUtils::Embed, and is turned on with `-DINN_PERL_FILTERS=ON`.  To write
filters and authentication hooks in Python instead, install Python 3.3.0 or
later with its embedding files and use `-DINN_PYTHON_FILTERS=ON`; INN++
does not support Python 2.  See [BUILDING.md](BUILDING.md).

For support for authenticated control messages, you will need either PGP
or GnuPG, the latter being recommended.  See `INSTALL` for more details.

The original INN's `ovdb` overview storage method (Berkeley DB) and news
reading over TLS (OpenSSL or LibreSSL) are not yet available in INN++: the
CMake build does not build them.  For the `hissqlite` history storage method
or the `ovsqlite` overview storage method, you'll need SQLite 3.24.0 or
later.  If you have zlib available, you can also compress overview before it's
stored into `ovsqlite`, and enable the COMPRESS capability, an NNTP
extension.  To support SASL authentication to **nnrpd** or to feed newsgroups to
an IMAP server with SASL authentication, you'll need the Cyrus SASL libraries.
INN++ can also check passwords against a Kerberos KDC; for this, you will need
Kerberos libraries.

# Getting Started

A news server can be a fairly complicated piece of software to set up just
because of the wide variety of pieces that have to be configured (who is
authorized to read from the server, what newsgroups it carries, and how
the articles are stored on disk at a bare minimum, and if the server isn't
completely stand-alone -- and very few servers are -- both incoming and
outgoing feeds have to be set up and tested).  Be prepared to take some
time to understand what's going on and how all the pieces fit together.
If you have any specific suggestions for documentation, or comments about
things that are unclear, please send them to the INN maintainers (see
[Reporting Bugs](#reporting-bugs) below).

## Download

INN++ is developed on GitHub at <https://github.com/ecarew/inn2>.  Releases
are published as tags there; source archives of each one are on the
[releases page](https://github.com/ecarew/inn2/releases) and the
[tags page](https://github.com/ecarew/inn2/tags).  Please build from a
tagged release rather than from a branch: the branches are under active
development and may not be stable at any given time.  To check out a
release with Git:

    git clone https://github.com/ecarew/inn2.git
    cd inn2
    git checkout <tag>

INN++ is not distributed in binary form.

The original INN, written in C and built with **autoconf**, is released
by the InterNetNews project at <https://downloads.isc.org/isc/inn/>.

## Build and Install

[BUILDING.md](BUILDING.md) covers building, testing and
`cmake --install`; see
[INSTALL](https://www.eyrie.org/~eagle/software/inn/docs/install.html)
for step-by-step instructions for setting up and configuring a news
server.

**Docker** is a supported target.  INN++ is built, tested and installed as
root in a Debian container (`support/docker/Dockerfile.linux`), and
`support/docker-install-checks.sh` checks that the installed server starts
as news and serves port 119 inside the container.  A ready-made image is
planned; until then, the same Dockerfile and `cmake --install` are the way
to build one.

## Documentation

INN++ contains all of the original INN documentation, as well as
documentation of its own.

**INN's documentation** is unchanged.  INN comes with a very complete set
of man pages; there is a man page
for every configuration file and program that comes with INN.  (If you
find one that doesn't have a man page, that's a bug.  Please do report
it.)  When trying to figure out some specific problem, reading the man
pages for all of the configuration files involved is a very good start.

URL: <https://www.eyrie.org/~eagle/software/inn/docs/> (online
documentation)

INN's documentation is in `doc/pod` (the man pages and guides such as
`install.pod`, from which the man pages are generated), `doc/man`, and the
other files in `doc`.  Where it describes building with `configure` and
`make`, INN++ builds with CMake instead; see [BUILDING.md](BUILDING.md).

**INN++'s own documentation** is, at this point, mostly notes on the
conversion of the original code base to C++ and CMake:

- [BUILDING.md](BUILDING.md): building, testing and installing INN++.
- [CONVERSION_NOTES.md](CONVERSION_NOTES.md): the conversion's decisions,
  findings, upstream bugs fixed along the way, proposals and their
  outcomes, and the conversion log.
- [CONVERSION_PLAYBOOK.md](CONVERSION_PLAYBOOK.md): the order of work and
  the tools, written for the next conversion of this kind.
- [CPP_GUIDELINES.md](CPP_GUIDELINES.md): C++ style and class design for
  new code and the refactoring phase.
- `doc/spec`: specifications for deployment (Docker, virtual machines) and
  the performance comparison with INN.
- `doc/perf`: performance reports.

# Reporting Bugs

We're interested in all bug reports.  Not just on the programs, but on the
documentation too.  Please report non-security bugs on the project's
GitHub issue tracker:

    https://github.com/ecarew/inn2/issues

(patches are certainly welcome, see below).  Even if you post to Usenet,
please open an issue there as well.

INN++ is independent of the original INN (see
[Who's Responsible](#whos-responsible--who-to-thank)): please don't send
INN++ bug reports to the InterNetNews project or its maintainers.

Security issues must be reported through the private form on the project's
[GitHub Security
Advisories](https://github.com/ecarew/inn2/security/advisories/) page, as
they should not be made public until a fix is deployed.

If you have general "how do I do this" questions or problems configuring
your server that you don't believe are due to a bug in INN++, you can post
them to news.software.nntp, where many experienced INN users read.  Please
say that you are running INN++, not the original INN.

# Testing and Development

## Source Code

INN++ is maintained using the Git version control system.  Its repository
is located at <https://github.com/ecarew/inn2>.  A local copy of the
current development source can be obtained by cloning it using a Git
client:

    git clone https://github.com/ecarew/inn2.git

For the history of the original INN, from which INN++ was converted, see
the InterNetNews Git repository at <https://github.com/InterNetNews/inn>.
Its [daily snapshots](https://archives.eyrie.org/software/inn/snapshots/)
are of the original INN, not INN++.

## Contributing Code

If you have a non-security patch or a utility that you'd like to be considered
for inclusion into INN++, please open a pull request on the project's GitHub
repository:

    https://github.com/ecarew/inn2/pulls

Patches included with a bug report as described above should follow the
same procedure.

Patches related to security issues must not be made
public.  A dedicated private form on the [GitHub Security
Advisories](https://github.com/ecarew/inn2/security/advisories/) of the
project must be used instead.

Please submit all patches against the current main branch, not against a
stable release.  The development version of INN is often much different
than the last stable release.

Also please take a look at
[HACKING](https://www.eyrie.org/~eagle/software/inn/docs/hacking.html)
for information about development and coding style.

Have fun!

# Who's Responsible / Who to Thank

INN++ is maintained by Evan Carew on GitHub at
<https://github.com/ecarew/inn2>; see its
[contributors page](https://github.com/ecarew/inn2/graphs/contributors) for
everyone who has worked on it.

INN++ is built on the original INN, but it is an independent project: the
InterNetNews maintainers and Internet Systems Consortium do not develop or
support it and have no obligation to do so.  Please report INN++ problems
here, not to them.  See
[CONTRIBUTORS](https://www.eyrie.org/~eagle/software/inn/docs/contributors.html)
for a long list of INN's past contributors as well as people from the
inn-workers mailing list who dedicated a lot of time and effort to it.
They deserve a big round of applause.  They've certainly got our thanks.

This product includes software developed by UUNET Technologies, Inc. and
by the University of California, Berkeley and its contributors.

Last, but certainly not least, Rich Salz, the original author of INN,
deserves a lion's share of the credit for writing INN in the first place
and making it the most popular news server software on the planet (no NNTP
yet to the moon, but we plan to be there first).

# Related Packages

INN users may also be interested in the following software packages that
work with INN or are based on it.  Please note that none of this software
is developed or maintained by ISC; we don't support it and generally can't
answer questions about it.

- **cleanfeed-ng**

  URL: <https://github.com/infybofh/cleanfeed-ng/> (maintained by Ivo Gandolfo)

  cleanfeed-ng is a maintained, lightweight continuation of the historical
  Cleanfeed filter.  It is designed for server-to-server Usenet transit
  filtering, with particular attention to predictable behaviour, low overhead,
  safe configuration changes, and useful diagnostics.

  cleanfeed-ng is an extremely powerful spam filter, probably the most widely
  used spam filter on Usenet currently.  It catches excessive multiposting and a
  host of other things, and is highly configurable.  Note that it requires the
  MD5 Perl module and that INN be built with Perl support (the **--with-perl**
  option to configure).

  Since 2026, cleanfeed-ng has continued the historical work of Cleanfeed,
  originally developed by Jeremy Nixon, who maintained it until 1998.
  Further development was then taken on by Marco d'Itri until 2002, followed
  by later updates and maintenance by Steve Crook from 2007 to 2020 (at
  <http://www.mixmin.net/cleanfeed/> but no longer maintained).

  A Python-based variant of Cleanfeed, named PyClean, also exists and
  can be found at <https://github.com/crooks/PyClean>.

- **GUP (Group Update Program)**

  URL: <https://github.com/rfc1036/gup>

  GUP provides a way for your peers to update their `newsfeeds` entries as
  they want without having to ask you to edit the configuration file all the
  time.  It's useful when feeding peers take limited and very specific
  feeds that change periodically.

- **innduct**

  URL:
  <https://www.chiark.greenend.org.uk/ucgi/~ian/git-manpage/innduct.git/innduct.8>
  (maintained by Ian Jackson)

  A possible replacement for **innfeed**, **innxmit** and **nntpsend**
  that quickly and reliably streams Usenet articles to a remote site.
  **innduct** is designed to be robust and not to lose any articles (when
  offered to peers) in case it unexpectedly dies, contrary to **innfeed**.
  It also permits a realtime feed, contrary to **innxmit** or **nntpsend**.

- **NewsPortal**

  URL: <https://gitlab.com/yamo-nntp/newsportal>

  A PHP-based web news reader that works as a front-end to a regular news
  server such as INN and lets people read and post without learning a news
  reader.

  NewsPortal was originally written by Florian Amrhein
  <https://amrhein.eu/newsportal/doc/> and is now maintained by Stéphane
  Grégoire who notably did a great job fixing a few bugs and adapting
  it for use on smartphones.

- **PersonalINN**

  URL: <http://www.ritual.org/summer/pinn/>

  PersonalINN is a version of INN modified for personal use and with a
  friendly GUI built on top of it.  It is available for NEXTSTEP or OPENSTEP
  only, unfortunately.

- **suck**

  URL: <https://github.com/lazarus-pkgs/suck>

  **suck** is a separate package for downloading a news feed via a reading
  connection (rather than via a direct NNTP or UUCP feed) and sending
  outgoing local posts via POST.  It's intended primarily for personal or
  small-organization news servers who get their news via an ISP and are too
  small to warrant setting up a regular news feed.

# Supporting the INN Effort

Note that INN is supported by Internet Systems Consortium, and although it
is free for use and redistribution and incorporation into vendor products
and export and anything else you can think of, it costs money to produce.
That money comes from ISPs, hardware and software vendors, companies who
make extensive use of the software, and generally kind-hearted folk such
as yourself.

Internet Systems Consortium has also commissioned a DHCP server
implementation and handles the official support/release of BIND.  You can
learn more about the ISC's goals and accomplishments from the web page at
<https://www.isc.org/>.

Russ Allbery\
Katsuhiro Kondou\
<inn@isc.org>
