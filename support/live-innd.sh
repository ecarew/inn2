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

# On a coverage build, add this run's profiles to the ones verify-cxx.sh
# collected (run it first; it clears the directory).
if grep -q '^INN_COVERAGE:BOOL=ON' "$build/CMakeCache.txt" 2>/dev/null; then
    mkdir -p "$build/coverage/raw"
    export LLVM_PROFILE_FILE="$build/coverage/raw/%m-%p.profraw"
fi
# Canonical path: TMPDIR may end in "/", and fastrm rejects a base path
# containing "//" ("bad base path").
tmp=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/live-innd.XXXXXX")" && pwd -P)
port=${LIVE_INND_PORT:-11119}
fail=0
innd_pid=
nnrpd_pid=

res() {
    printf '  %-42s %s\n' "$1" "$2"
    [[ "$2" == PASS* ]] || fail=1
}

cleanup() {
    [ -n "$nnrpd_pid" ] && kill "$nnrpd_pid" 2>/dev/null
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
    -e "s|^docancels:.*|docancels: \"auth\"|" \
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
ovfeed:*:Tf,WnteO:ovfeed
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

# gencancel without -n reads the original article from history and the
# spool to find its newsgroups; innd then processes the cancel it generates.
tok3=$("$bin/expire/grephistory" "<live3@live.test>" 2>/dev/null)
"$bin/control/gencancel" -f tester@live.test "<live3@live.test>" \
    >"$tmp/cancel3" 2>>"$tmp/ctlinnd.log"
# The scratch inn.conf sets docancels: "auth": the default "require-auth"
# acts like "none" without Cancel-Lock support, so cancels would be accepted
# but never processed (inn.conf(5)).
# Upstream bug (same in the original C): gencancel copies the Newsgroups
# value from the wire-format spool with its trailing \r, and innd refuses the
# resulting header.  Accept the \r here and strip it before sending, so the
# test still checks innd's cancel processing.
label="gencancel finds the article's newsgroups"
ok grep -qE $'^Newsgroups: test\\.live\r?$' "$tmp/cancel3"
canart=$(printf 'Path: live.test!not-for-mail\n'; tr -d '\r' <"$tmp/cancel3")
printf '#! rnews %d\n%s\n' "$(( ${#canart} + 1 ))" "$canart" \
    | "$bin/frontends/rnews" >>"$tmp/rnews.log" 2>&1
sleep 1
label="innd honors the gencancel article"
ok sh -c "! '$bin/frontends/sm' '$tok3' >/dev/null 2>&1"

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

# --- nnrpd (readers) ------------------------------------------------------------

# nnrpd refuses a pipe on stdin ("Can't get your name"), so run it as a
# daemon on the next port and talk NNTP to it with nc.  readers.conf lets
# 127.0.0.1 in unauthenticated and checks AUTHINFO with the built ckpasswd
# against INN's test passwd file (foo/foopass).
rport=$((port + 1))
cp "$root/tests/data/etc/passwd" "$tmp/etc/passwd"
# Reader tracking (track.c): nnrpd reads inn.conf at startup, so turn it on
# for this section and name the client in nnrpd.track (host:identity).
sed -i '' 's/^readertrack:.*/readertrack: true/' "$INNCONF"
printf 'localhost:tracker@live.test\n127.0.0.1:tracker@live.test\n' >"$tmp/etc/nnrpd.track"
cat >"$tmp/etc/readers.conf" <<EOF
auth "localhost" {
    hosts: "127.0.0.1"
    auth: "$bin/authprogs/ckpasswd -f $tmp/etc/passwd"
    default: "<local>"
}
access "everyone" {
    users: "*"
    newsgroups: "*"
}
EOF
"$bin/nnrpd/nnrpd" -D -f -p "$rport" >"$tmp/nnrpd.out" 2>&1 &
nnrpd_pid=$!
for _ in $(seq 1 25); do
    nc -z 127.0.0.1 "$rport" 2>/dev/null && break
    sleep 0.2
done
# nntp FILE: send the CRLF-terminated commands in $cmds, then QUIT; output
# goes to $tmp/FILE.
nntp() {
    { printf '%s' "$cmds" | sed 's/$/\r/'; printf 'QUIT\r\n'; } \
        | nc -w 10 127.0.0.1 "$rport" >"$tmp/$1" 2>&1
}
has() { grep -q "$2" "$tmp/$1"; }

cmds='CAPABILITIES
MODE READER
GROUP test.live
LISTGROUP test.live
ARTICLE <live2@live.test>
HEAD <live2@live.test>
BODY <live2@live.test>
STAT <live2@live.test>
OVER 1-
XOVER 1-
HDR Subject 1-
LIST NEWSGROUPS
LIST ACTIVE test.*
LIST OVERVIEW.FMT
LIST HEADERS
NEWGROUPS 20000101 000000
NEWNEWS * 20000101 000000
DATE
HELP
GROUP no.such.group
BOGUS
'
nntp reader.out
label="nnrpd greets and lists capabilities"
ok sh -c "grep -q '^200 .*ready' '$tmp/reader.out' && grep -q '^READER' '$tmp/reader.out' && grep -q '^OVER' '$tmp/reader.out'"
label="nnrpd GROUP and LISTGROUP"
ok sh -c "grep -qE '^211 [1-9][0-9]* [0-9]+ [0-9]+ test.live' '$tmp/reader.out'"
label="nnrpd ARTICLE, HEAD, BODY, STAT by message-ID"
ok sh -c "grep -q '^220 ' '$tmp/reader.out' && grep -q '^221 ' '$tmp/reader.out' && grep -q '^222 ' '$tmp/reader.out' && grep -q '^223 ' '$tmp/reader.out' && grep -q 'Body of live test article 2' '$tmp/reader.out'"
label="nnrpd OVER, XOVER and HDR"
ok sh -c "[ \$(grep -c '^224 ' '$tmp/reader.out') = 2 ] && grep -q '^225 ' '$tmp/reader.out' && grep -q 'live test 2' '$tmp/reader.out'"
label="nnrpd LIST variants"
ok sh -c "[ \$(grep -c '^215 ' '$tmp/reader.out') -ge 4 ] && grep -qE '^test.live[[:space:]]+live test group' '$tmp/reader.out'"
label="nnrpd NEWGROUPS and NEWNEWS"
ok sh -c "grep -q '^231 ' '$tmp/reader.out' && grep -q '^230 ' '$tmp/reader.out' && grep -q '^<live2@live.test>' '$tmp/reader.out'"
label="nnrpd DATE and HELP"
ok sh -c "grep -qE '^111 [0-9]{14}' '$tmp/reader.out' && grep -q '^100 ' '$tmp/reader.out'"
label="nnrpd rejects an unknown group and command"
ok sh -c "grep -q '^411 ' '$tmp/reader.out' && grep -q '^500 ' '$tmp/reader.out'"

cmds='AUTHINFO USER foo
AUTHINFO PASS foopass
'
nntp auth-good.out
cmds='AUTHINFO USER foo
AUTHINFO PASS barpass
'
nntp auth-bad.out
label="nnrpd AUTHINFO accepts a valid password (ckpasswd)"
ok has auth-good.out '^281 '
label="nnrpd AUTHINFO rejects a wrong password"
ok has auth-bad.out '^481 '

cmds='POST
Path: live.test!not-for-mail
From: reader@live.test
Newsgroups: test.live
Subject: posted through nnrpd
Message-ID: <nnrpdpost@live.test>

Posted through nnrpd.
.
'
nntp post.out
sleep 1
label="nnrpd POST reaches innd"
ok sh -c "grep -q '^340 ' '$tmp/post.out' && grep -q '^240 ' '$tmp/post.out' && '$bin/expire/grephistory' '<nnrpdpost@live.test>' >/dev/null 2>&1"

# COMPRESS DEFLATE (zlib.c): after the 206 reply the stream is compressed,
# which nc cannot speak, so only the reply is checked.
cmds='COMPRESS DEFLATE
'
nntp compress.out
label="nnrpd COMPRESS DEFLATE is accepted"
ok has compress.out '^206 '
label="nnrpd readertrack logs the tracked session"
ok sh -c "grep -qs 'Tracking Enabled (tracker@live.test)' '$tmp'/log/tracklogs/log-*"
label="nnrpd readertrack copies tracked posts to trackposts"
ok sh -c "grep -qs 'posttrack ok' '$tmp'/log/tracklogs/log-* && ls '$tmp'/log/trackposts/track.* >/dev/null"

# --- frontends that talk to nnrpd ----------------------------------------------

fe=$bin/frontends
"$fe/getlist" -h 127.0.0.1 -p "$rport" >"$tmp/getlist.out" 2>&1
label="getlist fetches the active list"
ok sh -c "grep -q '^test.live ' '$tmp/getlist.out'"
"$fe/getlist" -h 127.0.0.1 -p "$rport" newsgroups 'test.*' >"$tmp/getlist-ng.out" 2>&1
label="getlist newsgroups with a pattern"
ok sh -c "grep -q '^test.live' '$tmp/getlist-ng.out' && ! grep -q '^junk' '$tmp/getlist-ng.out'"
"$fe/getlist" -h 127.0.0.1 -p "$rport" active 'test.*' y >"$tmp/getlist-y.out" 2>&1
label="getlist active filtered by group type"
ok sh -c "grep -q '^test.live ' '$tmp/getlist-y.out'"

# inews posts through nnrpd; -h: the input holds the headers.
printf 'From: tester@live.test\nNewsgroups: test.live\nSubject: posted with inews\nMessage-ID: <inews1@live.test>\n\nPosted with inews.\n' \
    | "$fe/inews" -h -p "$rport" >"$tmp/inews.out" 2>&1
sleep 1
label="inews posts through nnrpd"
ok inhist inews1
label="inews refuses an article without Newsgroups"
ok sh -c "! { printf 'From: tester@live.test\nSubject: no groups\n\nBody.\n' | '$fe/inews' -h -p '$rport' >/dev/null 2>&1; }"

kill "$nnrpd_pid" 2>/dev/null
wait "$nnrpd_pid" 2>/dev/null

# --- frontends ---------------------------------------------------------------

# encode/decode: 7-bit encoding of a binary batch and back.
head -c 4096 /dev/urandom >"$tmp/binary"
"$fe/encode" <"$tmp/binary" >"$tmp/encoded"
"$fe/decode" <"$tmp/encoded" >"$tmp/decoded"
label="encode output is 7-bit"
ok sh -c "! LC_ALL=C grep -q '[^ -~[:space:]]' '$tmp/encoded'"
label="decode restores the encoded data"
ok cmp -s "$tmp/binary" "$tmp/decoded"

# innconfval: single values, shell/Perl/Tcl output, and checking.
label="innconfval prints a value"
ok sh -c "[ \"\$('$fe/innconfval' pathspool)\" = '$tmp/spool' ]"
label="innconfval -s prints shell assignments"
ok sh -c "'$fe/innconfval' -s | grep -q '^PATHSPOOL='"
label="innconfval -p prints Perl assignments"
ok sh -c "'$fe/innconfval' -p | grep -q '^\\\$pathspool = '"
label="innconfval -t prints Tcl assignments"
ok sh -c "'$fe/innconfval' -t | grep -q '^set inn_pathspool '"
label="innconfval -C accepts the scratch inn.conf"
ok "$fe/innconfval" -C
label="innconfval -v prints the version"
ok sh -c "'$fe/innconfval' -v | grep -q 'INN'"

# rnews: a gzip-compressed batch, and a batch from the incoming spool (-U).
art=$(printf 'Path: live.test!not-for-mail\nFrom: tester@live.test\nNewsgroups: test.live\nSubject: gzip batch\nMessage-ID: <gzip1@live.test>\nDate: %s\n\nBody of the gzip batch.\n' \
      "$(date -u '+%a, %d %b %Y %H:%M:%S +0000')")
printf '#! rnews %d\n%s\n' "$(( ${#art} + 1 ))" "$art" | gzip -c >"$tmp/batch.gz"
"$fe/rnews" <"$tmp/batch.gz" >>"$tmp/rnews.log" 2>&1
art=$(printf 'Path: live.test!not-for-mail\nFrom: tester@live.test\nNewsgroups: test.live\nSubject: spooled batch\nMessage-ID: <spool1@live.test>\nDate: %s\n\nBody of the spooled batch.\n' \
      "$(date -u '+%a, %d %b %Y %H:%M:%S +0000')")
printf '#! rnews %d\n%s\n' "$(( ${#art} + 1 ))" "$art" >"$tmp/spool/incoming/live.batch"
"$fe/rnews" -U >>"$tmp/rnews.log" 2>&1
ctl flush ''
sleep 1
label="rnews unpacks a gzip batch"
ok inhist gzip1
label="rnews -U processes the incoming spool"
ok sh -c "'$bin/expire/grephistory' '<spool1@live.test>' >/dev/null 2>&1 && [ ! -e '$tmp/spool/incoming/live.batch' ]"

# sm: article information, headers only, raw output.
tokg=$("$bin/expire/grephistory" '<gzip1@live.test>' 2>/dev/null)
label="sm -i prints the group and article number"
ok sh -c "'$fe/sm' -i '$tokg' | grep -Eq '^test.live: [0-9]+\$'"
label="sm -H prints only the headers"
ok sh -c "'$fe/sm' -H '$tokg' | grep -q '^Subject: gzip batch' && ! '$fe/sm' -H '$tokg' | grep -q 'Body of'"
label="sm -R prints the raw (wire) article"
ok sh -c "'$fe/sm' -R '$tokg' | grep -q \$'\r\$'"

# ovdb tools: built without Berkeley DB, ovdb_init, ovdb_server and
# ovdb_stat report that and fail; ovdb_monitor's stub just exits.
label="ovdb tools report missing Berkeley DB support"
ok sh -c "for t in ovdb_init ovdb_server ovdb_stat; do '$fe/'\$t 2>&1 </dev/null | grep -q 'Berkeley DB support not compiled' || exit 1; done && '$fe/ovdb_monitor' </dev/null"

# --- backends ---------------------------------------------------------------

be=$bin/backends
ctl flush ''
sleep 1
out=$tmp/spool/outgoing

# innxmit offers the file feed's articles back to innd, which already has
# them: every offer is refused.  It streams by default; -s turns streaming
# off and uses IHAVE.
cp "$out/filefeed" "$tmp/xmit.batch"
"$be/innxmit" -d -P "$port" 127.0.0.1 "$tmp/xmit.batch" >"$tmp/innxmit.out" 2>&1
label="innxmit streams (CHECK, all refused)"
ok sh -c "grep -q '^> CHECK' '$tmp/innxmit.out' && grep -q '^< 438' '$tmp/innxmit.out'"
cp "$out/filefeed" "$tmp/xmit.batch"
"$be/innxmit" -d -s -P "$port" 127.0.0.1 "$tmp/xmit.batch" >"$tmp/innxmit-s.out" 2>&1
label="innxmit -s uses IHAVE (all refused)"
ok sh -c "grep -q '^> IHAVE' '$tmp/innxmit-s.out' && grep -q '^< 435' '$tmp/innxmit-s.out'"
"$be/innxmit" -p 127.0.0.1 "$tmp/xmit.batch" >>"$tmp/innxmit.out" 2>&1
label="innxmit -p purges nothing from a valid batch"
ok sh -c "[ -s '$tmp/xmit.batch' ] || [ ! -e '$tmp/xmit.batch' ]"

# batcher turns a list of tokens into an rnews batch on standard output
# (and removes its input file once done, so give it a copy).
cut -d' ' -f1 "$out/filefeed" >"$tmp/tokens"
cp "$tmp/tokens" "$tmp/batcher.in"
"$be/batcher" -v live "$tmp/batcher.in" >"$tmp/batcher.out" 2>"$tmp/batcher.err"
label="batcher writes an rnews batch"
ok sh -c "[ \$(grep -c '^#! rnews ' '$tmp/batcher.out') -ge 5 ]"

# cvtbatch adds the requested fields to each token (live1 was cancelled
# earlier, so look for live2).
"$be/cvtbatch" -w nm <"$tmp/tokens" >"$tmp/cvtbatch.out" 2>&1
label="cvtbatch -w nm adds message-IDs"
ok sh -c "grep -q '<live2@live.test>' '$tmp/cvtbatch.out'"

# overchan writes overview data from innd's WnteO feed.  innd already
# writes overview itself, so overchan warns about that and rewrites the same
# entries.
"$be/overchan" "$out/ovfeed" >"$tmp/overchan.out" 2>&1
label="overchan reads the overview feed"
ok sh -c "grep -q 'timings [1-9]' '$tmp/overchan.out' && [ -n \"\$('$bin/storage/tdx-util' -o -n test.live 2>/dev/null)\" ]"

# buffchan reads funnel input, the data (one field by default) followed by
# the sites to send it to, and appends the data to one file per site.
mkdir -p "$tmp/bc"
printf 'first siteA\nsecond siteB siteA\nthird siteA\n' \
    | "$be/buffchan" -d "$tmp/bc" >"$tmp/buffchan.out" 2>&1
label="buffchan writes one file per site"
ok sh -c "[ \$(wc -l <'$tmp/bc/siteA') = 3 ] && grep -qx second '$tmp/bc/siteB'"

# inndf reports free space, history entries and overview usage.
label="inndf reports free space"
ok sh -c "'$be/inndf' '$tmp/spool' | grep -Eq '^[0-9]+'"
label="inndf -n counts history entries"
ok sh -c "'$be/inndf' -n | grep -Eq '^[0-9]+'"
label="inndf -o reports overview usage"
ok sh -c "'$be/inndf' -o | grep -q ."

# shlock: take a lock for this shell, refuse it for another live process,
# release it once the holder is gone.
lock=$tmp/run/LOCK.test
label="shlock takes a free lock"
ok "$be/shlock" -f "$lock" -p $$
sleep 30 & other=$!
label="shlock refuses a lock held by a live process"
ok sh -c "! '$be/shlock' -f '$lock' -p $other"
label="shlock -c checks without locking"
ok sh -c "! '$be/shlock' -c -f '$lock' -p $other"
kill "$other"; wait "$other" 2>/dev/null
echo "$other" >"$lock"
# shlock only replaces a lock whose inode changed in an earlier second than
# its own temporary file (a guard against a lock replaced mid-check).
sleep 1
label="shlock replaces a stale lock"
ok "$be/shlock" -f "$lock" -p $$

# shrinkfile keeps the end of a file, cut at a line boundary.
seq 1 5000 >"$tmp/shrink.txt"
"$be/shrinkfile" -s 1k "$tmp/shrink.txt" 2>>"$tmp/ctlinnd.log"
label="shrinkfile -s keeps the tail at a line start"
ok sh -c "[ \$(wc -c <'$tmp/shrink.txt') -le 1024 ] && tail -1 '$tmp/shrink.txt' | grep -qx 5000"

# actsync compares two active files and prints the ctlinnd commands needed.
printf 'test.live 0000000000 0000000001 y\ntest.new 0000000000 0000000001 y\n' \
    >"$tmp/active.remote"
# -p 0: no minimum share of unchanged groups (two of five change here).
"$be/actsync" -i /dev/null -o x -p 0 -v 0 "$tmp/db/active" "$tmp/active.remote" \
    >"$tmp/actsync.out" 2>&1
label="actsync lists the groups to add and remove"
ok sh -c "grep -q 'newgroup test.new' '$tmp/actsync.out' && grep -q 'rmgroup junk' '$tmp/actsync.out'"

# ninpaths: collect Path data, then report on it.
printf 'live.test!peer.a!peer.b!not-for-mail\nlive.test!peer.a!not-for-mail\n' \
    | "$be/ninpaths" -p -d "$tmp/paths.dump" 2>>"$tmp/ctlinnd.log"
"$be/ninpaths" -r live.test -u "$tmp/paths.dump" -v 2 >"$tmp/ninpaths.out" 2>&1
label="ninpaths reports the sites in its Path data"
ok sh -c "grep -q 'peer.a' '$tmp/ninpaths.out'"

# nntpget, innxbatch and innbind: only argument and error paths here
# (nntpget and innxbatch always use port 119; innbind binds for innd).
label="nntpget rejects conflicting options"
ok sh -c "! '$be/nntpget' -f /dev/null -t 990101 127.0.0.1 >/dev/null 2>&1"
label="innxbatch fails without a reachable server"
ok sh -c "! '$be/innxbatch' -t 5 127.0.0.1 '$batch' >/dev/null 2>&1"
label="innbind rejects a missing argument"
ok sh -c "! '$be/innbind' >/dev/null 2>&1"

gh() { "$bin/expire/grephistory" "<$1@live.test>" 2>/dev/null; }
tok() { gh "$1" | head -1; }

# --- innfeed ------------------------------------------------------------------

# Two peers: "leaf", a tinyleaf behind a Perl listener that forks one per
# connection (tinyleaf speaks only IHAVE, so innfeed falls back from
# streaming), and "self", the running innd (streaming; it already has every
# article, so each offer is refused).  innfeed runs in channel mode, reading
# the lines innd would send (token, message-ID, peer) from standard input,
# and exits once its input ends and the articles are sent.
lport=$((port + 2))
mkdir -p "$tmp/leaf" "$tmp/spool/innfeed"
leaf_up() {
    perl -MIO::Socket::INET -e '
        my ($port, @cmd) = @ARGV;
        my $l = IO::Socket::INET->new(LocalAddr => "127.0.0.1",
                                      LocalPort => $port, Listen => 5,
                                      ReuseAddr => 1) or die "listen: $!";
        $SIG{CHLD} = "IGNORE";
        while (my $c = $l->accept) {
            if (!fork) {
                open(STDIN, "<&", $c) and open(STDOUT, ">&", $c) or die;
                exec @cmd or die "exec: $!";
            }
            close $c;
        }' "$lport" "$bin/innd/tinyleaf" "$tmp/leaf" 2>>"$tmp/leaf.err" &
    leaf_pid=$!
    sleep 0.5
}
leaf_down() { kill "$leaf_pid" 2>/dev/null; wait "$leaf_pid" 2>/dev/null; }
cat >"$tmp/etc/innfeed.conf" <<EOF
backlog-directory: $tmp/spool/innfeed
peer leaf {
    ip-name:     127.0.0.1
    port-number: $lport
}
peer self {
    ip-name:     127.0.0.1
    port-number: $port
}
EOF
# Fresh articles for innfeed: earlier checks cancel or remove some of the
# live articles, and offering a peer an article that is gone makes innfeed
# send it an empty one.
for i in 1 2 3; do send_one "innfeed$i"; done
ctl flush ''
sleep 1
# funnel PEER: the channel lines innd would send for those articles.
funnel() {
    local i t
    for i in 1 2 3; do
        t=$(gh "innfeed$i" | head -1)
        [ -n "$t" ] && echo "$t <innfeed$i@live.test> $1"
    done
}
inf() { "$bin/innfeed/innfeed" -c "$tmp/etc/innfeed.conf" \
            -l "$tmp/innfeed.log" -S "$tmp/innfeed.status" \
            -p "$tmp/run/innfeed-test.pid" "$@"; }
# feed PEER WANT: run innfeed with the funnel lines for PEER on its input,
# and keep the input open until the leaf spool holds WANT articles (or ten
# seconds pass), so innfeed has time to connect and send before its input
# ends; at the end of input it flushes, sends QUIT and exits.
leafcount() { ls "$tmp/leaf" | wc -l | tr -d ' '; }
# peerstat PEER FIELD: a counter from the status file innfeed rewrites at
# exit (its statistics otherwise go to syslog).
peerstat() {
    awk -v peer="$1" -v field="$2" '
        $0 == peer { inpeer = 1; next }
        inpeer && /^$/ { exit }
        inpeer { for (i = 1; i < NF; i++)
                     if ($i == field ":") { print $(i + 1); exit } }' \
        "$tmp/innfeed.status"
}
feed() {
    { funnel "$1"
      for _ in $(seq 1 50); do
          [ "$(leafcount)" -ge "$2" ] && break
          sleep 0.2
      done
    } | inf >>"$tmp/innfeed.out" 2>&1
}

label="innfeed -C accepts its configuration"
ok inf -C
printf 'peer broken {\n    port-number: notanumber\n' >"$tmp/etc/innfeed-bad.conf"
label="innfeed -C rejects a broken configuration"
ok sh -c "! '$bin/innfeed/innfeed' -C -c '$tmp/etc/innfeed-bad.conf' >/dev/null 2>&1"

leaf_up
sent=$(funnel leaf | wc -l | tr -d ' ')
feed leaf "$sent"
got=$(leafcount)
label="innfeed delivers to an IHAVE-only peer ($got/$sent)"
ok sh -c "[ '$sent' -gt 0 ] && [ '$got' = '$sent' ]"

# Not checked yet (see CLAUDE.md, innfeed): offering tinyleaf the same
# articles again, and replaying a backlog with innfeed -x.

# Streaming to innd, which already has every article.
feed self 999
refused=$(peerstat self refused)
label="innfeed streams to innd (refused ${refused:-?} duplicates)"
ok sh -c "awk '/^self\$/,/^\$/' '$tmp/innfeed.status' | grep -q 'is streaming: yes' && [ '${refused:-0}' -ge '$sent' ]"

# Peer down: innfeed writes the articles to the peer's backlog file.
leaf_down
rm -f "$tmp/leaf"/*
feed leaf 999
label="innfeed backlogs articles while the peer is down"
ok sh -c "[ -s '$tmp/spool/innfeed/leaf' ] || [ -s '$tmp/spool/innfeed/leaf.output' ]"

# --- expire tools -------------------------------------------------------------


# prunehistory overwrites a token in the history text with spaces (it then
# lacks its field separator, which grephistory warns about), so the article
# is remembered but no longer retrievable through history.
echo '<live4@live.test>' | "$bin/expire/prunehistory" 2>>"$tmp/ctlinnd.log"
label="prunehistory removes the history token"
ok sh -c "'$bin/expire/grephistory' '<live4@live.test>' 2>/dev/null | grep -qx /dev/null"

# fastrm removes the articles whose tokens it reads.
t5=$(tok live5)
echo "$t5" | "$bin/expire/fastrm" "$tmp/spool/articles" 2>>"$tmp/ctlinnd.log"
label="fastrm removes an article by token"
ok sh -c "! '$bin/frontends/sm' '$t5' >/dev/null 2>&1"

# expire.ctl: remember nothing, expire everything at once.  With
# groupbaseexpiry (the sample inn.conf default) expire handles the history
# file and expireover expires the articles group by group.
printf '/remember/:0\n*:A:0:0:0\n' >"$tmp/etc/expire.ctl"
# With 0 days, articles that arrived in the current second are kept, so let
# the clock move past the last arrival first.
sleep 2
t2=$(tok live2)
"$bin/expire/expire" -v1 >"$tmp/expire.out" 2>&1
label="expire rewrites history (drops remembered entries)"
ok sh -c "grep -q 'Entries expired' '$tmp/expire.out' && ! '$bin/expire/grephistory' '<live4@live.test>' >/dev/null 2>&1"
"$bin/expire/expireover" >"$tmp/expireover.out" 2>&1
label="expireover expires the articles"
ok sh -c "grep -q 'Articles dropped' '$tmp/expireover.out' && ! '$bin/frontends/sm' '$t2' >/dev/null 2>&1"
label="expireover empties the group's overview"
ok sh -c "[ -z \"\$('$bin/storage/tdx-util' -o -n test.live 2>/dev/null)\" ]"

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
