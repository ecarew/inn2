#! /bin/sh
#
# Test suite for contrib/auth_pass.  The program needs a network connection
# as its standard input; tcp-stdin provides one.  A successful login cannot be
# tested without a password hash that this test can read, so these are the
# failures, which are the paths that must not let anyone in.

. "$(dirname "$0")/tap.sh"
echo "1..14"

prog=$CONTRIB_DIR/auth_pass
long300=$(head -c 300 /dev/zero | tr '\0' a)

# Run the program with a TCP connection as standard input.
tcp() {
    "$TCP_STDIN" "$prog" "$@"
}
tcp_status_is() {
    want=$1
    shift
    tcp "$@" >out 2>err
    got=$?
    test "$got" = "$want"
}
# Succeeds when the status is one of the two.
tcp_status_in() {
    a=$1 b=$2
    shift 2
    tcp "$@" >out 2>err
    got=$?
    test "$got" = "$a" || test "$got" = "$b"
}

check "without a connection it fails" status_is 1 "$prog" nosuchuserxyz pw
check "and says it cannot get the peer" \
    grep -q '^cant getpeername()::nosuchuserxyz:+:!\*$' err

check "an unknown user is refused" tcp_status_is 2 nosuchuserxyz pw
check "and logged as a no-access record" \
    grep -q '::nosuchuserxyz:+:!\*$' err
check "an empty user name is refused" tcp_status_is 2 "" pw

# root's password field is "*" or "x" (no hash to compare), or a hash that
# this password does not match.  It must never be accepted.
check "root with a wrong password is refused" tcp_status_in 3 4 root nopassword
check "no success record for root" \
    sh -c '! grep -q ":RP:" err'
check "an empty password is refused for root" tcp_status_in 3 4 root ""

check "a too long user name is refused, not cut short" \
    tcp_status_is 5 "$long300" pw
check "and says so" grep -q 'too long' err
check "a too long password is refused" tcp_status_is 5 nosuchuserxyz "$long300"

check "with no arguments, no user name can be read" tcp_status_is 5
check "the prompt is on standard output" grep -q '^Username: ' out
check "with only a user name, no password can be read" \
    tcp_status_is 5 nosuchuserxyz

exit "$failed"
