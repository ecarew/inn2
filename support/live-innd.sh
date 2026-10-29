#!/bin/bash
#
#  Run a real innd in a scratch installation, as the current user, and drive
#  it with INN's own clients.  Exercises the parts of innd that only run in a
#  live server (nc.cc, rc.cc, cc.cc, site.cc, ...).
#
#  Usage: support/live-innd.sh [build-dir]     (default: build)
#
#  Everything lives in a temporary directory: inn.conf points every path
#  there, runasuser/runasgroup are the current user and group (innd requires
#  them to match), and innd listens on 127.0.0.1 on an unprivileged port, so
#  neither root nor innbind is involved.  Prints one line per check and a
#  RESULT line, like support/verify-cxx.sh.  Set KEEP=1 to keep the directory.

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/${1:-build}
bin=$build
tmp=$(mktemp -d "${TMPDIR:-/tmp}/live-innd.XXXXXX")
port=${LIVE_INND_PORT:-11119}
fail=0
innd_pid=

res() {
    printf '  %-42s %s\n' "$1" "$2"
    [[ "$2" == PASS* ]] || fail=1
}

cleanup() {
    if [ -n "$innd_pid" ] && kill -0 "$innd_pid" 2>/dev/null; then
        kill "$innd_pid" 2>/dev/null
        sleep 1
        kill -9 "$innd_pid" 2>/dev/null
    fi
    if [ -n "$KEEP" ]; then
        echo "kept: $tmp"
    else
        rm -rf "$tmp"
    fi
}
trap cleanup EXIT

export INNCONF=$tmp/etc/inn.conf

# --- Scratch installation ---------------------------------------------------

mkdir -p "$tmp"/{etc,db,run,log,tmp,http} \
         "$tmp"/spool/{articles,overview,incoming,outgoing,archive}

user=$(id -un)
group=$(id -gn)
sed -e "s|/usr/local/news|$tmp|g" \
    -e "s|^pathhistory:.*|pathhistory: $tmp/db|" \
    -e "s|^port:.*|port: $port|" \
    -e "s|^noreader:.*|noreader: true|" \
    "$root/samples/inn.conf" >"$INNCONF"
cat >>"$INNCONF" <<EOF
runasuser:   $user
runasgroup:  $group
bindaddress: 127.0.0.1
server:      127.0.0.1
domain:      live.test
fromhost:    live.test
EOF

cat >"$tmp/etc/newsfeeds" <<'EOF'
ME:*::
filefeed:*:Tf,Wnm:filefeed
EOF
cat >"$tmp/etc/incoming.conf" <<'EOF'
streaming: true
max-connections: 8
peer localhost {
    hostname: "127.0.0.1, localhost"
}
EOF
cat >"$tmp/etc/storage.conf" <<'EOF'
method tradspool {
    newsgroups: *
    class: 0
}
EOF
cat >"$tmp/etc/readers.conf" <<'EOF'
auth "localhost" { hosts: "127.0.0.1" default: "<local>" }
access "localhost" { users: "<local>" newsgroups: "*" }
EOF
printf '%s 0000000000 0000000001 y\n' control control.cancel junk \
    test.live >"$tmp/db/active"
printf '%s\tlive test group\n' control control.cancel junk test.live \
    >"$tmp/db/newsgroups"
: >"$tmp/db/history"
"$bin/expire/makedbz" -i -o >"$tmp/makedbz.log" 2>&1 \
    && res "scratch installation created" PASS \
    || { res "scratch installation created" "FAIL (makedbz)"; \
         cat "$tmp/makedbz.log"; echo "RESULT: FAIL"; exit 1; }

# --- Start innd ---------------------------------------------------------------

"$bin/innd/innd" -d -f >"$tmp/innd.out" 2>&1 &
innd_pid=$!
up=false
for _ in $(seq 1 50); do
    if "$bin/frontends/ctlinnd" -s mode >/dev/null 2>&1; then
        up=true
        break
    fi
    kill -0 "$innd_pid" 2>/dev/null || break
    sleep 0.2
done
if $up; then
    res "innd starts and answers ctlinnd" PASS
else
    res "innd starts and answers ctlinnd" FAIL
    sed 's/^/    /' "$tmp/innd.out" | tail -20
    echo "RESULT: FAIL"
    exit 1
fi

# --- Feed articles over NNTP with rnews ---------------------------------------

ctl() { "$bin/frontends/ctlinnd" -s "$@" >>"$tmp/ctlinnd.log" 2>&1; }

narts=5
batch=$tmp/batch
: >"$batch"
for i in $(seq 1 $narts); do
    art=$(printf 'Path: live.test!not-for-mail\nFrom: tester@live.test\nNewsgroups: test.live\nSubject: live test %d\nMessage-ID: <live%d@live.test>\nDate: %s\n\nBody of live test article %d.\n' \
          "$i" "$i" "$(date -u '+%a, %d %b %Y %H:%M:%S +0000')" "$i")
    printf '#! rnews %d\n%s\n' "$(( ${#art} + 1 ))" "$art" >>"$batch"
done
"$bin/frontends/rnews" <"$batch" >"$tmp/rnews.log" 2>&1
ctl flush ''
sleep 1

found=0
for i in $(seq 1 $narts); do
    "$bin/expire/grephistory" "<live$i@live.test>" >/dev/null 2>&1 \
        && found=$((found + 1))
done
[ $found = $narts ] && res "rnews articles reach history" "PASS ($found/$narts)" \
    || res "rnews articles reach history" "FAIL ($found/$narts)"

stored=0
for i in $(seq 1 $narts); do
    tok=$("$bin/expire/grephistory" "<live$i@live.test>" 2>/dev/null)
    "$bin/frontends/sm" "$tok" 2>/dev/null | grep -q "live test article $i\." \
        && stored=$((stored + 1))
done
[ $stored = $narts ] && res "articles retrievable from spool (sm)" "PASS ($stored/$narts)" \
    || res "articles retrievable from spool (sm)" "FAIL ($stored/$narts)"

fed=$(grep -c '<live[0-9]*@live.test>' "$tmp/spool/outgoing/filefeed" 2>/dev/null)
[ "${fed:-0}" = $narts ] && res "file feed records each article" "PASS ($fed/$narts)" \
    || res "file feed records each article" "FAIL (${fed:-0}/$narts)"

# Same batch again: innd must refuse every article as a duplicate.
"$bin/frontends/rnews" <"$batch" >>"$tmp/rnews.log" 2>&1
ctl flush ''
sleep 1
fed2=$(grep -c '<live[0-9]*@live.test>' "$tmp/spool/outgoing/filefeed" 2>/dev/null)
[ "${fed2:-0}" = $narts ] && res "duplicates refused" PASS \
    || res "duplicates refused" "FAIL (feed now ${fed2:-0})"

# --- Control channel (ctlinnd) ------------------------------------------------

ctl newgroup test.created y tester@live.test
grep -q '^test.created ' "$tmp/db/active" \
    && res "ctlinnd newgroup adds to active" PASS \
    || res "ctlinnd newgroup adds to active" FAIL
ctl throttle "live test"
mode_t=$("$bin/frontends/ctlinnd" mode 2>&1 | head -1)
ctl go "live test"
mode_r=$("$bin/frontends/ctlinnd" mode 2>&1 | head -1)
echo "throttled: $mode_t / running: $mode_r" >>"$tmp/ctlinnd.log"
if echo "$mode_t" | grep -qi throttled && echo "$mode_r" | grep -qi running; then
    res "ctlinnd throttle and go" PASS
else
    res "ctlinnd throttle and go" "FAIL ($mode_t | $mode_r)"
fi
ctl rmgroup test.created
grep -q '^test.created ' "$tmp/db/active" \
    && res "ctlinnd rmgroup removes from active" FAIL \
    || res "ctlinnd rmgroup removes from active" PASS

# --- Shut down ------------------------------------------------------------------

"$bin/frontends/ctlinnd" -s shutdown "live test done" >/dev/null 2>&1
for _ in $(seq 1 50); do
    kill -0 "$innd_pid" 2>/dev/null || break
    sleep 0.2
done
if kill -0 "$innd_pid" 2>/dev/null; then
    res "ctlinnd shutdown stops innd" FAIL
else
    wait "$innd_pid" 2>/dev/null
    res "ctlinnd shutdown stops innd" PASS
    innd_pid=
fi

if [ $fail = 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
    exit 1
fi
