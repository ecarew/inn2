#! /bin/sh
#
# Test suite for contrib/newsresp, against a fake NNTP server on the
# loopback address (fake-nntp), on the port the server prints.

. "$(dirname "$0")/tap.sh"
echo "1..14"

prog=$CONTRIB_DIR/newsresp
fake=$(dirname "$TCP_STDIN")/fake-nntp

# Start the fake server (with an optional banner), leaving its port in $port
# and its commands in server.err.
startserver() {
    rm -f server.out server.err
    "$fake" "$@" >server.out 2>server.err &
    server=$!
    n=0
    while ! test -s server.out && test $n -lt 50; do
        sleep 0.1
        n=$((n + 1))
    done
    port=$(head -n 1 server.out)
}

check "no host is a usage error" status_is 1 "$prog"
check "usage text" grep -q '^Usage: ' err
check "zero articles is a usage error" status_is 1 "$prog" -n 0 localhost
check "a bad article count is a usage error" status_is 1 "$prog" -n x localhost

startserver
check "a session with two articles succeeds" \
    status_is 0 "$prog" -n 2 -p "$port" 127.0.0.1
wait $server
check "it shows the server's greeting" grep -q '<<< 200 ready' out
check "it sent two IHAVEs" test "$(grep -c '^ihave$' server.err)" = 2
check "and the article ends and QUIT" \
    sh -c 'grep -qx "\\." server.err && grep -qx quit server.err'
check "the closing reply is shown" grep -q '<<< 205 bye' out

startserver "502 no service"
check "an unexpected greeting fails" \
    status_is 1 "$prog" -p "$port" 127.0.0.1
wait $server
check "and says what was expected" \
    grep -q 'Expected 200 or 201, received 502' out

# Nothing listens on the port the server used once it exited.
check "a refused connection fails" status_is 1 "$prog" -p "$port" 127.0.0.1
check "and reports the error" grep -qi 'connect' err

check "an unknown host fails" status_is 1 "$prog" no-such-host.invalid

exit "$failed"
