# Building and testing INN++

INN++ is INN 2.8.0 converted to C++20 with a CMake-only build (branch
`cmake-build`).  This file is for someone who wants to build it and check the
conversion.  Why things are the way they are: [CONVERSION_NOTES.md](CONVERSION_NOTES.md).
Installing is under [Install](#install); configuring a news server is
covered by INN's own
[INSTALL](https://www.eyrie.org/~eagle/software/inn/docs/install.html).

## Requirements

CMake 4.3 or later, a C/C++20 compiler (Apple clang or GCC), Ninja, Perl,
Python 3 (used by the verification script), flex, bison, SQLite 3, zlib and
Boost 1.92 or later (header-only libraries, nothing to link).
No Autoconf or Make: `./configure` is not used.

**macOS** (Xcode command-line tools for the compiler, SQLite and zlib):

```sh
xcode-select --install
brew install cmake ninja boost
```

**Debian or Ubuntu** (Debian trixie is what is tested):

```sh
sudo apt-get install build-essential ninja-build perl python3 python3-venv \
    flex bison libsqlite3-dev zlib1g-dev netcat-openbsd procps curl bzip2
```

Debian's own CMake is older than 4.3; take it from PyPI (as
`support/docker/Dockerfile.linux` does) or from Kitware's packages:

```sh
python3 -m venv ~/cmake && ~/cmake/bin/pip install 'cmake>=4.3'
export PATH=~/cmake/bin:$PATH
```

Debian trixie's Boost (`libboost-dev`) is 1.83, older than the 1.92 INN++
requires; install the official release's headers (nothing is compiled),
checked against the SHA-256 Boost publishes in
`boost_1_92_0.tar.bz2.json` beside the archive:

```sh
curl -fLO https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.bz2
echo "5c1d40cb8e19adbf740a4ec2da35b3e58f3f5804b1dce44deb53df72193cbc6c  boost_1_92_0.tar.bz2" | sha256sum -c
tar xjf boost_1_92_0.tar.bz2 && cd boost_1_92_0
./bootstrap.sh && sudo ./b2 --prefix=/usr/local --with-headers install
```

A distribution's own package works once it is 1.92 or later.

**Optional:** `ccache` (used automatically when installed), PAM, Cyrus SASL
and Kerberos development files (found if present: `libpam0g-dev`,
`libsasl2-dev`, `libkrb5-dev` on Debian).  libcanlock (Cancel-Lock) is
used when it is installed.  For the Perl and Python filters (options
below): Perl with ExtUtils::Embed and Python 3.3 or later with its
embedding files (`libperl-dev`, `python3-dev` on Debian; macOS's Perl and
Homebrew's Python have them).  TLS, Berkeley DB (ovdb) and systemd are not
yet wired into CMake.

## Build

```sh
git clone --branch cmake-build https://github.com/ecarew/inn2.git
cd inn2
cmake -S . -B build -G Ninja
cmake --build build
```

That builds INN's programs (49, or 48 without Kerberos: auth_krb5) and its
three libraries.  CMake runs its own platform checks and generates
`config.h`, the sample configuration files, the scripts, innshellvars and
INN::Config into `build/`.

Build options (`-D<option>=<value>` on the first `cmake` command):

- `INN_PREFIX` (default `/usr/local/news`): where INN lives; the other
  directories follow it.
- `INN_LOG_COMPRESS` (default `gzip`): how old logs are compressed:
  `gzip`, `bzip2`, `compress` or `cat`.
- `INN_CONTRIB` (default `OFF`): build the programs in `contrib/`.
- `INN_OPTIONAL_PROGRAMS` (default `OFF`): build feedone and sys2nf.
- `INN_KEYWORDS` (default `OFF`): generate Keywords for overview.
- `INN_TESTS` (default `ON`): build the tests.
- `INN_SETGID_INEWS` (default `OFF`): install inews setgid news (mode
  2555) so local users can post without nnrpd authentication
  (configure's `--enable-setgid-inews`).
- `INN_PERL_FILTERS` (default `OFF`): embed Perl in innd and nnrpd for
  innd's and nnrpd's filters and nnrpd's `perl_auth` and `perl_access`
  hooks (configure's `--with-perl`).
- `INN_PYTHON_FILTERS` (default `OFF`): embed Python, for the same filters
  and nnrpd's `python_auth`, `python_access` and `python_dynamic` hooks
  (configure's `--with-python`).

The filters are written as INN documents them in `doc/pod/hook-perl.pod`
and `doc/pod/hook-python.pod`.  Sample filters are installed either way,
in `bin/filter`.

## Install

```sh
sudo cmake --install build
```

installs into `INN_PREFIX` what `make install` does, with the same owners
and modes: everything owned by `news:news`, and `innbind`, the only setuid
program, owned by root with mode 4550.  The `news` user and group must
exist first.  On a first install it also creates the data directories, a
minimal `active` and `newsgroups`, and an empty history database.

- The prefix is chosen when configuring (`-DINN_PREFIX=...`);
  `cmake --install --prefix` has no effect.
- A configuration file is installed only if none is there, so a reinstall
  keeps your configuration and data (there are no `.OLD` copies).
- Staging for a package: `DESTDIR=/tmp/stage cmake --install build`, as
  your own user; the owners and innbind's setuid bit are then left to the
  package (the install says what to set).
- A build configured with `INN_RUNAS_USER`/`INN_RUNAS_GROUP` refuses to
  install.
- Man pages and the documentation are not installed yet.

Then set `domain` in `etc/inn.conf` if the host name has no domain part,
configure INN following its INSTALL, and start the server as news:

```sh
sudo -u news /usr/local/news/bin/rc.news
```

innd binds port 119 through innbind; it never runs as root.

## Test

```sh
ctest --test-dir build
```

runs INN's test programs (written in C, built with the rest; `-DINN_TESTS=OFF`
leaves them out).  Expected skips: `lib/canlock` (needs libcanlock) and
`lib/newsuser` (needs root; see below).  The tests take turns even under
`ctest -j`: they share directories and fixed ports.

The full verification, which is what each commit was checked with:

```sh
support/verify-cxx.sh build
```

The script checks the build (exported symbols, C linkage), runs INN's test
programs (written in C) against the C++ libraries, and runs the shell tests.
It prints one line per check and `RESULT: PASS` or `RESULT: FAIL`.
Expected SKIPs: `gencancel matches original C` (needs an original
Autoconf/Make build to compare with), `lib/canlock` (needs libcanlock),
`lib/newsuser` (needs root; see below).

**Live test.**  Starts innd, nnrpd, innfeed and the other programs against a
scratch installation in a temporary directory, as your own user:

```sh
cmake -S . -B build-live -G Ninja \
    -DINN_RUNAS_USER=$(id -un) -DINN_RUNAS_GROUP=$(id -gn) \
    -DINN_PERL_FILTERS=ON -DINN_PYTHON_FILTERS=ON
cmake --build build-live
support/live-innd.sh build-live
```

With the two filter options it also checks innd's Perl and Python filters
and nnrpd's hooks, using the filters in `tests/data`; without them those
checks are skipped.  `support/cxx-tools/commit-verified.sh` refuses a
`build-live` without them.

Never use `INN_RUNAS_USER`/`INN_RUNAS_GROUP` for a build meant for real use:
they make INN run as that account instead of `news`.

Run one test script at a time: they use fixed local ports (11119 and up).

**Root-path checks** (programs started as root must drop to the news user;
programs that write INN's data must refuse root):

```sh
sudo support/root-checks.sh          # macOS or Linux, uses build-live
support/docker-root-checks.sh        # Linux, as root in a Docker container
```

**Install checks** (Linux, as root in a Docker container): installs with
`cmake --install`, checks the owners and modes, that innbind is the only
setuid file, that the sample filters are in `bin/filter`, that rc.news
starts innd as news on port 119, and that a reinstall keeps the
configuration:

```sh
support/docker-install-checks.sh
```

**On Linux without a Linux machine** (Docker, Debian trixie, as an ordinary
user who is not the news user):

```sh
support/docker-linux-checks.sh       # the working tree
support/docker-linux-checks.sh -c    # only the last commit, as a clone gets
```

Its live builds have the Perl and Python filters, so the filter checks run
on Linux too.

**Sanitizers and coverage** (coverage needs clang):

```sh
cmake -S . -B build-asan -G Ninja -DINN_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
support/verify-cxx.sh build-asan

cmake -S . -B build-cov -G Ninja -DINN_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug \
    -DINN_RUNAS_USER=$(id -un) -DINN_RUNAS_GROUP=$(id -gn)
support/verify-cxx.sh build-cov && support/live-innd.sh build-cov
support/coverage-report.sh build-cov --html
```

## Comparing with the original

Branch `main` is upstream INN, unmodified.  Every converted file was renamed
`.c` to `.cc` in its own commit, so `git log --follow -p <file>.cc` shows the
original and each change, and `git diff main -- <file>.c <file>.cc` the whole
difference.  Commit messages list the checks that ran before each commit.
