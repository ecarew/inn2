# Building and testing INN++

INN++ is INN 2.8.0 converted to C++20 with a CMake-only build (branch
`cmake-build`).  This file is for someone who wants to build it and check the
conversion.  Why things are the way they are: [CONVERSION_NOTES.md](CONVERSION_NOTES.md).
How to install INN for real use is not covered yet: the CMake build has no
install rules so far.

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
`libsasl2-dev`, `libkrb5-dev` on Debian).  Perl and Python filters, TLS,
Berkeley DB (ovdb), libcanlock and systemd are not yet wired into CMake.

## Build

```sh
git clone --branch cmake-build https://github.com/ecarew/inn2.git
cd inn2
cmake -S . -B build -G Ninja
cmake --build build
```

That builds INN's programs (49, or 48 without Kerberos: auth_krb5) and its
three libraries.  CMake runs its own platform checks and generates
`config.h` and the other generated files into `build/`.

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
    -DINN_RUNAS_USER=$(id -un) -DINN_RUNAS_GROUP=$(id -gn)
cmake --build build-live
support/live-innd.sh build-live
```

Never use `INN_RUNAS_USER`/`INN_RUNAS_GROUP` for a build meant for real use:
they make INN run as that account instead of `news`.

Run one test script at a time: they use fixed local ports (11119 and up).

**Root-path checks** (programs started as root must drop to the news user;
programs that write INN's data must refuse root):

```sh
sudo support/root-checks.sh          # macOS or Linux, uses build-live
support/docker-root-checks.sh        # Linux, as root in a Docker container
```

**On Linux without a Linux machine** (Docker, Debian trixie, as an ordinary
user who is not the news user):

```sh
support/docker-linux-checks.sh       # the working tree
support/docker-linux-checks.sh -c    # only the last commit, as a clone gets
```

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
