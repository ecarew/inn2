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

# An exploder (Tx) feed runs a program that receives text from innd; this
# one appends everything to explode.out, so ctlinnd send can be checked.
printf '#!/bin/sh\ncat >>%s/explode.out\n' "$tmp" >"$tmp/explode.sh"
chmod +x "$tmp/explode.sh"
cat >"$tmp/etc/newsfeeds" <<EOF
ME:*::
filefeed:*:Tf,Wnm:filefeed
explode:!*:Tx:$tmp/explode.sh
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

"$bin/innd/innd" -f >"$tmp/innd.out" 2>&1 &
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
    cat "$tmp/innd.out" "$tmp"/log/errlog "$tmp"/log/news 2>/dev/null \
        | sed 's/^/    /' | tail -20
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

# --- More ctlinnd commands ----------------------------------------------------

# send_one ID: send one article <ID@live.test> to test.live through rnews.
send_one() {
    local art
    art=$(printf 'Path: live.test!not-for-mail\nFrom: tester@live.test\nNewsgroups: test.live\nSubject: %s\nMessage-ID: <%s@live.test>\nDate: %s\n\nBody of %s.\n' \
          "$1" "$1" "$(date -u '+%a, %d %b %Y %H:%M:%S +0000')" "$1")
    printf '#! rnews %d\n%s\n' "$(( ${#art} + 1 ))" "$art" \
        | "$bin/frontends/rnews" >>"$tmp/rnews.log" 2>&1
}
inhist() { "$bin/expire/grephistory" "<$1@live.test>" >/dev/null 2>&1; }
infeed() { ctl flush filefeed; sleep 1
           grep -q "<$1@live.test>" "$tmp/spool/outgoing/filefeed"; }
mode() { "$bin/frontends/ctlinnd" mode 2>&1 | head -1; }
ok() { if "$@" 2>>"$tmp/ctlinnd.log"; then res "$label" PASS; else res "$label" FAIL; fi; }

# Server state: pause; reserve then throttle with the reserved reason.
label="ctlinnd pause and go"
ok sh -c "'$bin/frontends/ctlinnd' -s pause r1 && '$bin/frontends/ctlinnd' mode | head -1 | grep -qi paused && '$bin/frontends/ctlinnd' -s go r1 && '$bin/frontends/ctlinnd' mode | head -1 | grep -qi running"
label="ctlinnd reserve, then throttle"
ok sh -c "'$bin/frontends/ctlinnd' -s reserve r2 && '$bin/frontends/ctlinnd' -s throttle r2 && '$bin/frontends/ctlinnd' mode | head -1 | grep -qi throttled && '$bin/frontends/ctlinnd' -s go r2"
label="ctlinnd throttle refused while reserved"
ok sh -c "'$bin/frontends/ctlinnd' -s reserve r3 && ! '$bin/frontends/ctlinnd' -s throttle other; '$bin/frontends/ctlinnd' -s reserve ''"

# Articles and history.
tok1=$("$bin/expire/grephistory" "<live1@live.test>" 2>/dev/null)
ctl cancel "<live1@live.test>"
label="ctlinnd cancel removes the article"
ok sh -c "! '$bin/frontends/sm' '$tok1' >/dev/null 2>&1"
now=$(date +%s)
tok2=$("$bin/expire/grephistory" "<live2@live.test>" 2>/dev/null)
ctl addhist "<added@live.test>" "$now" 0 "$now" "$tok2"
label="ctlinnd addhist adds a history entry"
ok inhist added

# Newsgroups.
ctl changegroup test.live m
label="ctlinnd changegroup sets the group flag"
ok grep -q '^test.live [0-9]* [0-9]* m$' "$tmp/db/active"
ctl changegroup test.live y
label="ctlinnd renumber"
ok ctl renumber test.live
printf 'test.live 3\n' >"$tmp/lowmark"
ctl lowmark "$tmp/lowmark"
label="ctlinnd lowmark sets the low mark"
ok grep -q '^test.live [0-9]* 0*3 y$' "$tmp/db/active"

# Feeds: inspection, drop and begin, exploder send, reload.
label="ctlinnd checkfile accepts newsfeeds"
ok ctl checkfile
label="ctlinnd name lists the feed channels"
ok sh -c "'$bin/frontends/ctlinnd' name '' | grep -q filefeed"
label="ctlinnd feedinfo describes a feed"
ok sh -c "'$bin/frontends/ctlinnd' feedinfo filefeed | grep -q filefeed"
label="ctlinnd flushlogs"
ok ctl flushlogs
ctl drop filefeed
send_one whiledropped
label="ctlinnd drop stops a feed"
ok sh -c "inhist() { '$bin/expire/grephistory' \"<\$1@live.test>\" >/dev/null 2>&1; }; inhist whiledropped && ! grep -q '<whiledropped@live.test>' '$tmp/spool/outgoing/filefeed'"
ctl begin filefeed
send_one afterbegin
label="ctlinnd begin restarts a feed"
ok infeed afterbegin
ctl send explode "live send test"
sleep 1
label="ctlinnd send reaches an exploder feed"
ok grep -q 'live send test' "$tmp/explode.out"
printf 'newfeed:*:Tf,Wnm:newfeed\n' >>"$tmp/etc/newsfeeds"
ctl reload newsfeeds "live reload"
label="ctlinnd reload picks up a new feed"
ok sh -c "'$bin/frontends/ctlinnd' feedinfo newfeed | grep -q newfeed"
label="ctlinnd param changes a setting"
ok ctl param c 14

# Status reporting.
label="ctlinnd status writes a status file"
# innd writes the status file from its main loop (STATUSmainloophook, run
# when select() returns) once the interval has passed, so wait past the
# interval and then cause activity.
rm -f "$tmp/http/inn_status.html"
ctl status 1
sleep 1.5
send_one statusping
for _ in $(seq 1 10); do
    [ -s "$tmp/http/inn_status.html" ] && break
    sleep 0.5
done
ctl status off
label="ctlinnd status writes a status file"
ok test -s "$tmp/http/inn_status.html"
label="ctlinnd stathist logs history statistics"
ok sh -c "'$bin/frontends/ctlinnd' -s stathist '$tmp/stathist.out' && '$bin/expire/grephistory' '<live2@live.test>' >/dev/null; '$bin/frontends/ctlinnd' -s stathist off; test -e '$tmp/stathist.out'"
label="ctlinnd timer on and off"
ok sh -c "'$bin/frontends/ctlinnd' -s timer 1 && '$bin/frontends/ctlinnd' -s timer off"

# Connections.
ctl reject "live reject"
send_one whilerejected
label="ctlinnd reject refuses new connections"
ok sh -c "! '$bin/expire/grephistory' '<whilerejected@live.test>' >/dev/null 2>&1"
ctl allow "live reject"
send_one afterallow
label="ctlinnd allow accepts them again"
ok inhist afterallow
label="ctlinnd hangup refuses a bad channel"
ok sh -c "! '$bin/frontends/ctlinnd' -s hangup 999"
label="ctlinnd readers and logmode"
ok sh -c "'$bin/frontends/ctlinnd' -s readers n 'live test' && '$bin/frontends/ctlinnd' -s readers y '' && '$bin/frontends/ctlinnd' -s logmode"

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
