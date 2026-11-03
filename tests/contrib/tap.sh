# Shared helpers for the contrib tests (sourced).  They run in a scratch
# directory.

# Where the programs are: in the tests' sandbox that is ../contrib, from
# tests/ (run in the sandbox's tests directory), else set CONTRIB_DIR.
CONTRIB_DIR=$(cd "${CONTRIB_DIR:-../contrib}" && pwd) || exit 1
TCP_STDIN=${TCP_STDIN:-$PWD/contrib/tcp-stdin}

count=0
failed=0

# check <description> <command...>: ok when the command succeeds.
check() {
    desc=$1
    shift
    count=$((count + 1))
    if "$@"; then
        echo "ok $count - $desc"
    else
        echo "not ok $count - $desc"
        failed=1
    fi
}

# Make a scratch directory and enter it; removed at exit.
scratch=$(mktemp -d "${TMPDIR:-/tmp}/contrib-test.XXXXXX") || exit 1
trap 'rm -rf "$scratch"' EXIT
cd "$scratch" || exit 1

# Succeeds when "$@" exits with status $1.
status_is() {
    want=$1
    shift
    "$@" >out 2>err </dev/null
    got=$?
    test "$got" = "$want"
}
