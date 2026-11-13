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
dv=

res() {
    printf '  %-42s %s\n' "$1" "$2"
    [[ "$2" == PASS* ]] || fail=1
}

cleanup() {
    [ -n "$nnrpd_pid" ] && kill "$nnrpd_pid" 2>/dev/null
    [ -n "$dv" ] && "$root/support/dovecot-imapfeed.sh" stop "$dv"
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
    -e "s|^linecountfuzz:.*|linecountfuzz: 1|" \
    "$build/samples/inn.conf" >"$INNCONF"
# On a build with keyword generation (INN_KEYWORDS), turn it on and put
# Keywords in overview, so the keywords checks below run.
keywords=false
if grep -q '^INN_KEYWORDS:BOOL=ON' "$build/CMakeCache.txt" 2>/dev/null; then
    keywords=true
    sed -i.bak -e 's|^keywords:.*|keywords: true|' \
        -e 's|^extraoverviewadvertised:.*|extraoverviewadvertised: [ Keywords ]|' \
        "$INNCONF" && rm -f "$INNCONF.bak"
fi
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
if "$bin/expire/makedbz" -i -o >"$tmp/makedbz.log" 2>&1; then
    res "scratch installation created" PASS
else
    res "scratch installation created" "FAIL (makedbz)"
    cat "$tmp/makedbz.log"
    echo "RESULT: FAIL"
    exit 1
fi

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
if [ $found = $narts ]; then
    res "rnews articles reach history" "PASS ($found/$narts)"
else
    res "rnews articles reach history" "FAIL ($found/$narts)"
fi

stored=0
for i in $(seq 1 $narts); do
    tok=$("$bin/expire/grephistory" "<live$i@live.test>" 2>/dev/null)
    "$bin/frontends/sm" "$tok" 2>/dev/null | grep -q "live test article $i\." \
        && stored=$((stored + 1))
done
if [ $stored = $narts ]; then
    res "articles retrievable from spool (sm)" "PASS ($stored/$narts)"
else
    res "articles retrievable from spool (sm)" "FAIL ($stored/$narts)"
fi

fed=$(grep -c '<live[0-9]*@live.test>' "$tmp/spool/outgoing/filefeed" 2>/dev/null)
if [ "${fed:-0}" = $narts ]; then
    res "file feed records each article" "PASS ($fed/$narts)"
else
    res "file feed records each article" "FAIL (${fed:-0}/$narts)"
fi

# Same batch again: innd must refuse every article as a duplicate.
"$bin/frontends/rnews" <"$batch" >>"$tmp/rnews.log" 2>&1
ctl flush ''
sleep 1
fed2=$(grep -c '<live[0-9]*@live.test>' "$tmp/spool/outgoing/filefeed" 2>/dev/null)
if [ "${fed2:-0}" = $narts ]; then
    res "duplicates refused" PASS
else
    res "duplicates refused" "FAIL (feed now ${fed2:-0})"
fi

# --- Control channel (ctlinnd) ------------------------------------------------

ctl newgroup test.created y tester@live.test
if grep -q '^test.created ' "$tmp/db/active"; then
    res "ctlinnd newgroup adds to active" PASS
else
    res "ctlinnd newgroup adds to active" FAIL
fi
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
if grep -q '^test.created ' "$tmp/db/active"; then
    res "ctlinnd rmgroup removes from active" FAIL
else
    res "ctlinnd rmgroup removes from active" PASS
fi

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
# (sed -i takes different arguments in BSD and GNU sed; write a new copy.)
sed 's/^readertrack:.*/readertrack: true/' "$INNCONF" >"$INNCONF.new" \
    && mv "$INNCONF.new" "$INNCONF"
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

# Refused posts: a duplicate, refused by innd (its reply, CRLF and all, is
# the reason), and a From address without a dot in its domain, refused by
# nnrpd itself.  Each reason is sent as one 441 line: every line of the
# session must be a status line.
cmds='POST
Path: live.test!not-for-mail
From: reader@live.test
Newsgroups: test.live
Subject: posted through nnrpd again
Message-ID: <nnrpdpost@live.test>

Posted through nnrpd again.
.
POST
Path: live.test!not-for-mail
From: reader@localhost
Newsgroups: test.live
Subject: no dot in the From domain

Refused.
.
'
nntp post-refused.out
label="nnrpd sends a refused post's reason as one line"
ok sh -c "grep -q '^441 435 ' '$tmp/post-refused.out' && grep -q '^441 Address in From header field not in Internet syntax.\$' '$tmp/post-refused.out' && ! grep -vqE '^[0-9]{3} ' '$tmp/post-refused.out'"

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

# --- nnrpd Perl and Python hooks ------------------------------------------------
#
# The hooks only on a build with INN_PERL_FILTERS or INN_PYTHON_FILTERS
# (the first check, no access group, on every build).  nnrpd reads
# readers.conf for each connection, so each hook gets its own readers.conf.
# The hooks are in tests/data/nnrpd-hooks: authentication accepts foo/foopass
# and refuses the rest, access allows test.* only, the dynamic hook refuses
# posting, and the posting filter refuses "make money" subjects.

hk_perl=false
hk_python=false
grep -q '^#define DO_PERL 1' "$build/include/config.h" && hk_perl=true
grep -q '^#define DO_PYTHON 1' "$build/include/config.h" && hk_python=true
hk_denied=502
grep -q '^#define HAVE_SASL 1' "$build/include/config.h" && hk_denied=480
hooks=$root/tests/data/nnrpd-hooks
hkfilter=$(sed -n 's/^pathfilter:[[:space:]]*//p' "$INNCONF")
cp "$tmp/etc/readers.conf" "$tmp/etc/readers.conf.keep"
mkdir -p "$hkfilter"
# hk_readers LINE: readers.conf whose auth group for 127.0.0.1 also has
# LINE, with every user reading everything unless the hook says otherwise.
hk_readers() {
    cat >"$tmp/etc/readers.conf" <<EOF
auth "localhost" {
    hosts: "127.0.0.1"
    default: "<local>"
    $1
}
access "everyone" {
    users: "*"
    newsgroups: "*"
}
EOF
}
# hk_readers_only LINE: as hk_readers, with no access group (the hook gives
# the access).  Without access groups nnrpd crashed on every connection
# before the fix in nnrpd/perm.cc.
hk_readers_only() {
    cat >"$tmp/etc/readers.conf" <<EOF
auth "localhost" {
    hosts: "127.0.0.1"
    default: "<local>"
    $1
}
EOF
}
# hk_post ID SUBJECT FILE: post one article to test.live.
hk_post() {
    cmds="POST
Path: not-for-mail
From: reader@live.test
Newsgroups: test.live
Subject: $2
Message-ID: <$1@live.test>

Posted to test the nnrpd hooks.
.
"
    nntp "$3"
}
hk_auth_checks() {
    local lang=$1
    cmds='AUTHINFO USER foo
AUTHINFO PASS foopass
'
    nntp "hk-$lang-auth-good.out"
    cmds='AUTHINFO USER foo
AUTHINFO PASS barpass
'
    nntp "hk-$lang-auth-bad.out"
    label="nnrpd $lang auth hook accepts foo/foopass"
    ok has "hk-$lang-auth-good.out" '^281 '
    label="nnrpd $lang auth hook refuses a wrong password"
    ok has "hk-$lang-auth-bad.out" '^481 '
}
hk_access_checks() {
    local lang=$1
    cmds='GROUP test.live
GROUP control
'
    nntp "hk-$lang-access.out"
    label="nnrpd $lang access hook allows test.*"
    ok has "hk-$lang-access.out" '^211 .* test.live'
    # 480 (authenticate first) where nnrpd could still take AUTHINFO SASL,
    # since it then always offers authentication; 502 without SASL.
    label="nnrpd $lang access hook refuses other groups"
    ok has "hk-$lang-access.out" "^$hk_denied Read access denied"
}

# On every build: a readers.conf with an auth group but no access group.
hk_readers_only ""
cmds='GROUP test.live
'
nntp hk-noaccess.out
label="nnrpd answers with no access group in readers.conf"
ok has hk-noaccess.out '^[0-9][0-9][0-9] '

if $hk_perl; then
    hk_readers "perl_auth: \"$hooks/nnrpd_auth.pl\""
    hk_auth_checks Perl
    # Without filter_nnrpd.pl installed: perl_access does not depend on the
    # posting filter (upstream it did; CONVERSION_NOTES.md, "Upstream bugs
    # and quirks found").
    hk_readers_only "perl_access: \"$hooks/nnrpd_access.pl\""
    hk_access_checks Perl
    cp "$hooks/filter_nnrpd.pl" "$hkfilter/filter_nnrpd.pl"
    hk_readers ""
    hk_post hk-spam 'Make money fast' hk-spam.out
    hk_post hk-clean 'A clean post' hk-clean.out
    label="nnrpd Perl filter refuses a post with its reason"
    ok has hk-spam.out '^441 .*Spam is not acceptable here'
    label="nnrpd Perl filter accepts a clean post"
    ok sh -c "grep -q '^240 ' '$tmp/hk-clean.out' && sleep 1 && '$bin/expire/grephistory' '<hk-clean@live.test>' >/dev/null 2>&1"
    rm -f "$hkfilter/filter_nnrpd.pl"
fi
if $hk_python; then
    cp "$hooks"/nnrpd_*_test.py "$hkfilter/"
    hk_readers 'python_auth: "nnrpd_auth_test"'
    hk_auth_checks Python
    hk_readers_only 'python_access: "nnrpd_access_test"'
    hk_access_checks Python
    hk_readers 'python_dynamic: "nnrpd_dynamic_test"'
    cmds='GROUP test.live
'
    nntp hk-dyn-read.out
    hk_post hk-dyn 'Refused by the dynamic hook' hk-dyn-post.out
    label="nnrpd Python dynamic hook allows reading"
    ok has hk-dyn-read.out '^211 .* test.live'
    label="nnrpd Python dynamic hook refuses posting with its reason"
    ok sh -c "grep -q 'No posting through this server' '$tmp/hk-dyn-post.out' && ! grep -q '^240 ' '$tmp/hk-dyn-post.out'"
    rm -f "$hkfilter"/nnrpd_*_test.py
fi
mv "$tmp/etc/readers.conf.keep" "$tmp/etc/readers.conf"

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
# A carriage return, made here: sh -c runs dash on Debian, which lacks $'\r'.
cr=$(printf '\r')
ok sh -c "'$fe/sm' -R '$tokg' | grep -q '$cr\$'"

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
# shellcheck disable=SC2120  # extra innfeed options, when a caller has any
inf() { "$bin/innfeed/innfeed" -c "$tmp/etc/innfeed.conf" \
            -l "$tmp/innfeed.log" -S "$tmp/innfeed.status" \
            -p "$tmp/run/innfeed-test.pid" "$@"; }
# feed PEER WANT: run innfeed with the funnel lines for PEER on its input,
# and keep the input open until the leaf spool holds WANT articles (or ten
# seconds pass), so innfeed has time to connect and send before its input
# ends; at the end of input it flushes, sends QUIT and exits.
leafcount() { find "$tmp/leaf" -type f | wc -l | tr -d ' '; }
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

# The same articles again: tinyleaf refuses each with 435 before it is sent,
# and innfeed goes on to the next.  (tinyleaf used to answer 435 after the
# article, which innfeed treats as a protocol error: it put the connection
# to sleep and backlogged the rest.)
feed leaf 999
got=$(leafcount)
label="innfeed offers duplicates without backlogging them"
ok sh -c "[ '$got' = '$sent' ] && [ ! -s '$tmp/spool/innfeed/leaf' ] && [ ! -s '$tmp/spool/innfeed/leaf.output' ]"

# Streaming to innd, which already has every article.
feed self 999
# innfeed rewrites the status file only on connection events (and every ten
# minutes), so its counters can miss the last replies: on Linux it showed 3
# offered, 2 refused, 0 accepted.  Check what matters: innfeed streamed, innd
# refused duplicates, and accepted none.
refused=$(peerstat self refused)
accepted=$(peerstat self accepted)
label="innfeed streams to innd (refused ${refused:-?}, accepted ${accepted:-?})"
ok sh -c "awk '/^self\$/,/^\$/' '$tmp/innfeed.status' | grep -q 'is streaming: yes' && [ '${refused:-0}' -ge 1 ] && [ '${accepted:-1}' = 0 ]"

# Peer down: innfeed writes the articles to the peer's backlog file.
leaf_down
rm -f "$tmp/leaf"/*
feed leaf 999
label="innfeed backlogs articles while the peer is down"
ok sh -c "[ -s '$tmp/spool/innfeed/leaf' ] || [ -s '$tmp/spool/innfeed/leaf.output' ]"

# Peer back: innfeed -x (no channel input) sends the backlog and exits once
# it is empty.
leaf_up
inf -x </dev/null >>"$tmp/innfeed.out" 2>&1 &
xpid=$!
for _ in $(seq 1 50); do
    [ "$(leafcount)" -ge "$sent" ] && break
    sleep 0.2
done
got=$(leafcount)
kill "$xpid" 2>/dev/null; wait "$xpid" 2>/dev/null
leaf_down
label="innfeed -x sends the backlog ($got/$sent)"
ok sh -c "[ '$got' = '$sent' ]"

# --- imapfeed -----------------------------------------------------------------

# imapfeed delivers articles over LMTP and carries out cancels over IMAP.
# Its ports are fixed (IMAP 143, LMTP 2003, IPv4), so the dovecot from
# support/dovecot-imapfeed.sh listens on exactly those.  This script starts
# one as the current user, which works where that user may bind 143 (Linux
# with net.ipv4.ip_unprivileged_port_start=0); elsewhere start it with sudo
# beforehand and name its directory in LIVE_IMAP_DIR (see that script).
# Skipped when there is no dovecot or it cannot get the ports.  LMTP to
# news+GROUP saves into the mailbox GROUP; imapfeed cancels with SELECT,
# UID SEARCH, STORE \Deleted and CLOSE, which expunges.
imap=${LIVE_IMAP_DIR:-$tmp/imap}
imap_ok=false
if [ ! -x "$bin/innfeed/imapfeed" ]; then
    printf '  %-42s %s\n' "imapfeed checks" "skipped (no imapfeed)"
elif [ -n "$LIVE_IMAP_DIR" ]; then
    if [ -s "$imap/base_dir" ]; then
        imap_ok=true
    else
        printf '  %-42s %s\n' "imapfeed checks" \
            "skipped (no dovecot running in $imap)"
    fi
elif "$root/support/dovecot-imapfeed.sh" start "$imap" >"$tmp/dovecot.out" 2>&1
then
    dv=$imap
    imap_ok=true
else
    printf '  %-42s %s\n' "imapfeed checks" \
        "skipped (dovecot could not start on 143/2003)"
    tail -3 "$tmp/dovecot.out" "$imap/dovecot.log" 2>/dev/null | sed 's/^/    /'
    "$root/support/dovecot-imapfeed.sh" stop "$imap"
fi
if $imap_ok; then
        cat >"$tmp/etc/imapfeed.conf" <<EOF
backlog-directory: $tmp/spool/innfeed
deliver-username: news
deliver-authname: news
deliver-password: news-pw
deliver-rcpt-to: news+%s
peer imap {
    ip-name: 127.0.0.1
}
EOF
        for i in 1 2 3; do send_one "imap$i"; done
        ctl flush ''
        sleep 1
        # mailed ID: <ID@live.test> is in a message file in the test.live
        # mailbox (new/ or cur/, not one marked deleted but not yet expunged).
        mailed() {
            grep -rlq "^Message-ID: <$1@live.test>" \
                "$imap/mail/news"/.test.live/new \
                "$imap/mail/news"/.test.live/cur 2>/dev/null
        }
        # imapf ID...: run imapfeed with the channel lines for those articles,
        # keeping its input open until CHECK (a shell command) succeeds or
        # ten seconds pass.
        imapf() {
            local check=$1 i t
            shift
            { for i in "$@"; do
                  t=$(gh "$i" | head -1)
                  [ -n "$t" ] && echo "$t <$i@live.test> imap"
              done
              for _ in $(seq 1 50); do
                  sh -c "$check" && break
                  sleep 0.2
              done
            } | ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}log_path=$tmp/imapfeed.san" \
                UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}log_path=$tmp/imapfeed.san" \
                "$bin/innfeed/imapfeed" -c "$tmp/etc/imapfeed.conf" \
                  -l "$tmp/imapfeed.log" -S "$tmp/imapfeed.status" \
                  -p "$tmp/run/imapfeed.pid" >>"$tmp/imapfeed.out" 2>&1
        }
        md=$(declare -f mailed)
        imapf "imap='$imap'; $md; mailed imap1 && mailed imap2 && mailed imap3" \
            imap1 imap2 imap3
        label="imapfeed delivers over LMTP to dovecot"
        ok sh -c "imap='$imap'; $md; mailed imap1 && mailed imap2 && mailed imap3"

        # A cancel for imap1: innd files the control article, and imapfeed,
        # given it, removes imap1 from the mailbox over IMAP.
        art=$(printf 'Path: live.test!not-for-mail\nFrom: tester@live.test\nNewsgroups: test.live\nSubject: cmsg cancel <imap1@live.test>\nControl: cancel <imap1@live.test>\nMessage-ID: <imapcancel@live.test>\nDate: %s\n\nCancel.\n' \
              "$(date -u '+%a, %d %b %Y %H:%M:%S +0000')")
        printf '#! rnews %d\n%s\n' "$(( ${#art} + 1 ))" "$art" \
            | "$bin/frontends/rnews" >>"$tmp/rnews.log" 2>&1
        ctl flush ''
        sleep 1
        imapf "imap='$imap'; $md; ! mailed imap1" imapcancel
        label="imapfeed cancels over IMAP"
        ok sh -c "imap='$imap'; $md; ! mailed imap1 && mailed imap2"
        # imapfeed sends its standard error to its log, so a sanitizer
        # build's report goes to a file of its own; show it.
        for f in "$tmp"/imapfeed.san.*; do
            [ -f "$f" ] || continue
            res "imapfeed sanitizer report" FAIL
            grep -E 'ERROR|runtime error|^    #[0-9] |SUMMARY' "$f" | head -12 | sed 's/^/    /'
        done
    if [ -n "$dv" ]; then
        "$root/support/dovecot-imapfeed.sh" stop "$dv"
        dv=
    fi
fi

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

# --- Article routing: distributions, poison, hashfeed, newgroup ---------------
#
# Paths the in-memory article work moves (CONVERSION_NOTES.md, "innd's
# in-memory article", step 2): the Distribution list (ARTparsedist,
# DISTwanted, DISTwantany), poisoned groups (ARTpoisongroup), hashfeeds
# (HashFeedMatch, both hashes) and a newgroup for a group that does not
# exist yet (ARTsendthegroup).

# send_art ID GROUPS [HEADER...]: send <ID@live.test> to GROUPS through
# rnews, with any extra header lines.
send_art() {
    local id=$1 groups=$2 art extra=
    shift 2
    for h in "$@"; do extra="$extra$h"$'\n'; done
    art=$(printf 'Path: live.test!not-for-mail\nFrom: tester@live.test\nNewsgroups: %s\nSubject: %s\nMessage-ID: <%s@live.test>\nDate: %s\n%s\nBody of %s.\n' \
          "$groups" "$id" "$id" "$(date -u '+%a, %d %b %Y %H:%M:%S +0000')" \
          "$extra" "$id")
    printf '#! rnews %d\n%s\n' "$(( ${#art} + 1 ))" "$art" \
        | "$bin/frontends/rnews" >>"$tmp/rnews.log" 2>&1
}
# fed FEED ID: the file feed FEED (after a flush) lists <ID@live.test>.
fed() { grep -q "<$2@live.test>" "$tmp/spool/outgoing/$1" 2>/dev/null; }

cat >>"$tmp/etc/newsfeeds" <<EOF
distfeed:*/local:Tf,Wm:distfeed
nodist:*/!secret:Tf,Wm:nodist
poisonfeed:*,@test.nosuch:Tf,Wm:poisonfeed
newgrpfeed:!*,test.newgrp:Tf,Wm:newgrpfeed
hash1:*:Tf,Wm,Q1/2:hash1
hash2:*:Tf,Wm,Q2/2:hash2
qhash1:*:Tf,Wm,Q@1/2:qhash1
qhash2:*:Tf,Wm,Q@2/2:qhash2
sizes:*:Tf,WmbR:sizes
hdrfeed:*:Tf,WH:hdrfeed
EOF
ctl reload newsfeeds "live routing feeds"

send_art dist-local test.live 'Distribution: local'
send_art dist-world test.live 'Distribution: world'
send_art dist-both test.live 'Distribution: world, local'
send_art dist-none test.live
send_art dist-secret test.live 'Distribution: secret'
send_art poisoned test.live,test.nosuch
send_art unpoisoned test.live
send_art newgrp test.newgrp 'Control: newgroup test.newgrp' \
    'Approved: tester@live.test'
for i in 1 2 3 4 5 6 7 8; do send_art "hashed$i" test.live; done
ctl flush ''
sleep 1

label="site distributions: only listed ones, none is any"
ok bash -c "$(declare -f fed); tmp='$tmp'; fed distfeed dist-local && fed distfeed dist-both && fed distfeed dist-none && ! fed distfeed dist-world"
label="site distributions: a !negated one is refused, others sent"
ok bash -c "$(declare -f fed); tmp='$tmp'; ! fed nodist dist-secret && fed nodist dist-world && fed nodist dist-local"
label="a poisoned nonexistent group keeps a crosspost from a site"
ok bash -c "$(declare -f fed); tmp='$tmp'; ! fed poisonfeed poisoned && fed poisonfeed unpoisoned"
label="a newgroup for a new group reaches sites that want the group"
ok bash -c "$(declare -f fed); tmp='$tmp'; fed newgrpfeed newgrp && ! fed newgrpfeed unpoisoned"
# Each hashfeed pair splits the articles: every one goes to exactly one of
# the two, and (for these Message-IDs) both get some.
hashsplit() {
    local a=0 b=0 i
    for i in 1 2 3 4 5 6 7 8; do
        fed "$1" "hashed$i" && a=$((a + 1))
        fed "$2" "hashed$i" && b=$((b + 1))
        fed "$1" "hashed$i" && fed "$2" "hashed$i" && return 1
        fed "$1" "hashed$i" || fed "$2" "hashed$i" || return 1
    done
    [ $a -gt 0 ] && [ $b -gt 0 ]
}
label="hashfeed (MD5) splits articles between Q1/2 and Q2/2"
ok bash -c "$(declare -f fed hashsplit); tmp='$tmp'; hashsplit hash1 hash2"
label="hashfeed (quickhash) splits articles between Q@1/2 and Q@2/2"
ok bash -c "$(declare -f fed hashsplit); tmp='$tmp'; hashsplit qhash1 qhash2"

# A file feed of the Message-ID, size (b) and Xref group list (R): the size
# is the overview's Bytes, the list the article's own Xref entry.
label="a feed's b and R fields give the Bytes and the Xref groups"
ok bash -c "line=\$(grep '^<dist-local@live.test> ' '$tmp/spool/outgoing/sizes'); ov=\$('$bin/storage/tdx-util' -o -n test.live 2>/dev/null | grep '	<dist-local@live.test>	'); n=\$(printf '%s' \"\$ov\" | cut -f1); bytes=\$(printf '%s' \"\$ov\" | cut -f7); [ -n \"\$n\" ] && [ \"\$line\" = \"<dist-local@live.test> \$bytes test.live:\$n\" ]"

# A file feed of the headers (H): innd's generated Bytes line first, then
# the stored article's header fields, with CRLF turned into LF.
label="a feed's H field gives the article's headers, CR removed"
ok bash -c "f='$tmp/spool/outgoing/hdrfeed'; grep -q '^Bytes: [0-9]' \"\$f\" && grep -q '^Message-ID: <dist-local@live.test>\$' \"\$f\" && grep -q '^Distribution: local\$' \"\$f\" && grep -q '^Xref: [^ ]* test.live:[0-9]*\$' \"\$f\" && ! grep -q \$'\\r' \"\$f\""

# The ME entry's distributions decide what innd accepts at all.
sed -i.bak 's|^ME:\*::|ME:*/!rejectme::|' "$tmp/etc/newsfeeds"
ctl reload newsfeeds "live ME distributions"
send_art dist-rejectme test.live 'Distribution: rejectme'
send_art dist-accepted test.live 'Distribution: world'
mv "$tmp/etc/newsfeeds.bak" "$tmp/etc/newsfeeds"
ctl reload newsfeeds "live ME restored"
ctl flush ''
sleep 1
label="ME distributions: an unwanted one is rejected, others accepted"
ok sh -c "grep -q 'Unwanted distribution \"rejectme\"' '$tmp/log/news' && ! '$bin/frontends/sm' \"\$('$bin/expire/grephistory' '<dist-rejectme@live.test>' 2>/dev/null)\" >/dev/null 2>&1 && '$bin/expire/grephistory' '<dist-accepted@live.test>' >/dev/null 2>&1"

# --- Crossposts and long Xref header fields (ARTassignnumbers) ----------------
#
# 40 groups with 30-character names make an Xref longer than an article
# line may be (MAXARTLINELENGTH, 1000), so innd folds it; each group gets
# its own number.

xgroups=
for i in $(seq -w 1 40); do
    g=test.crosspost.long.name.g$i
    ctl newgroup "$g" y tester@live.test
    xgroups=${xgroups:+$xgroups,}$g
done
send_art crosspost "$xgroups"
ctl flush ''
sleep 1
xtok=$("$bin/expire/grephistory" '<crosspost@live.test>' 2>/dev/null)
"$bin/frontends/sm" "$xtok" 2>/dev/null | tr -d '\r' \
    | awk '/^Xref:/ { x = 1; print; next } x && /^[ \t]/ { print; next }
           { x = 0 }' >"$tmp/crosspost.xref"
label="a crosspost gets one Xref entry per group, each numbered"
ok sh -c "[ \$(grep -o 'test\.crosspost\.long\.name\.g[0-9]*:[0-9][0-9]*' '$tmp/crosspost.xref' | sort -u | wc -l) -eq 40 ]"
label="a long Xref header field is folded at 1000 characters"
# Two or more lines, each at most 998 characters (1000 with its CRLF), and
# every line after the first a continuation (starting with a space).
ok awk 'length > 998 { bad = 1 } NR > 1 && !/^ / { bad = 1 }
        END { exit bad || NR < 2 }' "$tmp/crosspost.xref"
label="the overview's Xref unfolds the long header field"
ok sh -c "'$bin/storage/tdx-util' -o -n test.crosspost.long.name.g40 2>/dev/null | grep -q 'Xref: [^	]*g01:[0-9]*[^	]* test.crosspost.long.name.g40:[0-9]'"

# --- Generated keywords (ARTmakeoverview, KEYgenerate) --------------------------
#
# Only on an INN_KEYWORDS build.  An article without a Keywords header field
# gets one in overview from its body's words: most frequent first, then
# longest, without the noise words.  Bodies under 100 bytes are skipped.
# Ties on frequency and length go alphabetically.  For the first body only
# the first three are checked: the noise words are not removed where the C
# library's basic regex lacks the GNU \| alternation the noise pattern uses
# (macOS).  kw-ties has no noise words, so its whole value is checked.

# send_body ID BODY [HEADER...]: like send_art, with the given body.
send_body() {
    local id=$1 body=$2 art extra=
    shift 2
    for h in "$@"; do extra="$extra$h"$'\n'; done
    art=$(printf 'Path: live.test!not-for-mail\nFrom: tester@live.test\nNewsgroups: test.live\nSubject: %s\nMessage-ID: <%s@live.test>\nDate: %s\n%s\n%s\n' \
          "$id" "$id" "$(date -u '+%a, %d %b %Y %H:%M:%S +0000')" \
          "$extra" "$body")
    printf '#! rnews %d\n%s\n' "$(( ${#art} + 1 ))" "$art" \
        | "$bin/frontends/rnews" >>"$tmp/rnews.log" 2>&1
}
# ovline ID: the article's overview line in test.live.
ovline() { "$bin/storage/tdx-util" -o -n test.live 2>/dev/null | grep "	<$1@live.test>	"; }
if $keywords; then
    words='Zebra zebra zebra zebra quokka quokka quokka narwhal narwhal axolotl.
The animals were there, about after also because, and before being here.'
    send_body kw-generated "$words"
    send_body kw-own "$words" 'Keywords: own, words'
    send_body kw-short 'Zebra zebra zebra quokka.'
    # 37 six-letter words seen once: more than std::sort's insertion-sort
    # cutoff, so an unbroken tie would show.
    send_body kw-ties 'Zebra zebra zebra quokka quokka narwhal narwhal axolotl walrus alpaca turtle badger python beaver salmon bobcat rabbit condor possum cougar pigeon coyote parrot donkey oyster falcon osprey ferret ocelot gerbil monkey gibbon marmot gopher magpie iguana lizard impala kitten jackal jaguar toucan turkey weasel wombat otter lemur.'
    ctl flush ''
    sleep 1
    label="keywords: generated from the body for overview"
    ok sh -c "$(declare -f ovline); bin='$bin'; ovline kw-generated | grep -q '	Keywords: zebra,quokka,narwhal,'"
    label="keywords: ties by frequency, length, then name"
    ok sh -c "$(declare -f ovline); bin='$bin'; ovline kw-ties | grep -q '	Keywords: zebra,narwhal,quokka,axolotl,alpaca,badger,beaver,bobcat,condor,cougar,coyote,donkey,falcon,ferret,gerbil,gibbon,gopher,iguana,impala,jackal,jaguar,kitten,lizard,magpie,marmot,monkey,ocelot,osprey,oyster,parrot,pigeon,possum,python,rabbit,salmon,toucan,turkey,turtle,walrus,weasel,wombat,lemur,otter	'"
    label="keywords: an article's own Keywords field is kept"
    ok sh -c "$(declare -f ovline); bin='$bin'; ovline kw-own | grep -q '	Keywords: own, words	'"
    label="keywords: none generated for a short body"
    ok sh -c "$(declare -f ovline); bin='$bin'; l=\$(ovline kw-short); [ -n \"\$l\" ] && ! printf '%s' \"\$l\" | grep -q 'Keywords:'"
else
    printf '  %-42s %s\n' "keywords checks" "skipped (INN_KEYWORDS off)"
fi

# --- Malformed articles (ARTclean) ---------------------------------------------
#
# Each is refused with its own reason in the news log.  linecountfuzz is 1
# in this inn.conf so that the Lines check runs; artcutoff is the sample's
# 10 days.

# send_raw HEADERS: send an article with exactly these header lines (each
# ending in a newline) through rnews.
send_raw() {
    local art
    art=$(printf '%s\nBody.\n' "$1")
    printf '#! rnews %d\n%s\n' "$(( ${#art} + 1 ))" "$art" \
        | "$bin/frontends/rnews" >>"$tmp/rnews.log" 2>&1
}
now=$(date -u '+%a, %d %b %Y %H:%M:%S +0000')
std='Path: live.test!not-for-mail
From: tester@live.test
Newsgroups: test.live'
# rnews checks the Subject and Message-ID itself, so these two are offered
# to innd directly.  ihave ID FILE HEADERS: offer <ID> with IHAVE, the
# article's header lines as given; innd's replies go to $tmp/FILE.
ihave() {
    { printf 'IHAVE <%s>\r\n' "$1"
      printf '%sBody.\n.\n' "$3" | sed 's/$/\r/'
      printf 'QUIT\r\n'; } | nc -w 10 127.0.0.1 "$port" >"$tmp/$2" 2>&1
}
ihave bad-nosubject@live.test ihave-nosubject.out "$std
Message-ID: <bad-nosubject@live.test>
Date: $now

"
ihave bad-mid@live.test ihave-badmid.out "$std
Subject: bad mid
Message-ID: <no-at-sign>
Date: $now

"
# A NUL byte in a header field or in the body: innd's parser refuses the
# article before any header value is used as a C string.  A shell variable
# cannot hold a NUL, so it goes in printf's format.
ihave_nul() {
    { printf 'IHAVE <%s>\r\n' "$1"
      printf "Path: live.test!not-for-mail\r\nFrom: tester@live.test\r\nNewsgroups: test.live\r\nMessage-ID: <$1>\r\nDate: $now\r\n$3.\r\n"
      printf 'QUIT\r\n'; } | nc -w 10 127.0.0.1 "$port" >"$tmp/$2" 2>&1
}
ihave_nul bad-nulhdr@live.test ihave-nulhdr.out 'Subject: nul\0here\r\n\r\nBody.\r\n'
ihave_nul bad-nulbody@live.test ihave-nulbody.out 'Subject: nul body\r\n\r\nBo\0dy.\r\n'
# An article over maxartsize (the sample's 1000000): refused while it is
# read, and its Message-ID remembered so it is not offered again.
{ printf 'IHAVE <bad-huge@live.test>\r\n'
  printf '%s\r\n' 'Path: live.test!not-for-mail' 'From: tester@live.test' \
      'Newsgroups: test.live' 'Subject: huge' 'Message-ID: <bad-huge@live.test>' \
      "Date: $now" ''
  yes "$(printf '%075d' 0)" | head -n 15000 | sed 's/$/\r/'
  printf '.\r\nQUIT\r\n'; } | nc -w 10 127.0.0.1 "$port" >"$tmp/ihave-huge.out" 2>&1
# reoffer ID FILE: offer <ID> again; innd's reply goes to $tmp/FILE.
reoffer() {
    printf 'IHAVE <%s>\r\nQUIT\r\n' "$1" \
        | nc -w 10 127.0.0.1 "$port" >"$tmp/$2" 2>&1
}
# remembertrash (the sample's true): a refused article's Message-ID is
# remembered too.
reoffer bad-nulhdr@live.test reoffer-nul.out
reoffer bad-huge@live.test reoffer-huge.out
send_raw "$std
Subject: bad lines
Message-ID: <bad-lines@live.test>
Date: $now
Lines: 9
"
send_raw "$std
Subject: bad date
Message-ID: <bad-date@live.test>
Date: not a date
"
send_raw "$std
Subject: bad injection date
Message-ID: <bad-injdate@live.test>
Date: $now
Injection-Date: not a date either
"
send_raw "$std
Subject: too old
Message-ID: <bad-old@live.test>
Date: Mon, 01 Jan 2001 00:00:00 +0000
"
send_raw "$std
Subject: from the future
Message-ID: <bad-future@live.test>
Date: Fri, 01 Jan 2100 00:00:00 +0000
"
send_raw "Path: live.test!not-for-mail
From: tester@live.test
Newsgroups: test.live:extra
Subject: colon in newsgroups
Message-ID: <bad-groups@live.test>
Date: $now
"
send_raw "$std
Subject: with expires
Message-ID: <good-expires@live.test>
Date: $now
Expires: Fri, 01 Jan 2100 00:00:00 +0000
"
send_raw "$std
Subject: with a bad expires
Message-ID: <good-badexpires@live.test>
Date: $now
Expires: whenever
"
ctl flush ''
sleep 1
rejected() { grep -q "$1" "$tmp/log/news"; }
label="innd rejects a missing required header field"
ok grep -q '^437 Missing "Subject" header field' "$tmp/ihave-nosubject.out"
label="innd rejects a bad Message-ID"
ok grep -q '^437 Bad "Message-ID" header field' "$tmp/ihave-badmid.out"
label="innd rejects a NUL in a header field or the body"
ok sh -c "grep -q '^437 Nul character in header' '$tmp/ihave-nulhdr.out' && grep -q '^437 Nul character in body' '$tmp/ihave-nulbody.out'"
label="an article with a NUL is not stored"
ok sh -c "for id in bad-nulhdr bad-nulbody; do ! '$bin/frontends/sm' \"\$('$bin/expire/grephistory' \"<\$id@live.test>\" 2>/dev/null)\" >/dev/null 2>&1 || exit 1; done"
label="innd rejects an article over maxartsize"
ok grep -q '^437 Article of [0-9]* bytes exceeds local limit of 1000000 bytes' "$tmp/ihave-huge.out"
label="the news log names a refused article by its Message-ID"
ok sh -c "grep -q ' - [^ ]* <bad-huge@live.test> 437 Article of ' '$tmp/log/news' && grep -q ' - [^ ]* <bad-nulhdr@live.test> 437 Nul character in header' '$tmp/log/news'"
label="innd remembers a refused article (remembertrash)"
ok sh -c "grep -q '^435 ' '$tmp/reoffer-nul.out' && grep -q '^435 ' '$tmp/reoffer-huge.out'"
label="innd rejects a wrong Lines count (linecountfuzz)"
ok rejected 'bad-lines@live.test.* Linecount 9 != 1 +- 1'
label="innd rejects a bad Date and a bad Injection-Date"
ok sh -c "grep -q 'bad-date@live.test.* Bad \"Date\" header field' '$tmp/log/news' && grep -q 'bad-injdate@live.test.* Bad \"Injection-Date\" header field' '$tmp/log/news'"
label="innd rejects articles too old (artcutoff) and from the future"
ok sh -c "grep -q 'bad-old@live.test.* Too old' '$tmp/log/news' && grep -q 'bad-future@live.test.* in the future' '$tmp/log/news'"
label="innd rejects a colon in Newsgroups"
ok rejected 'bad-groups@live.test.* Unwanted character in "Newsgroups"'
label="innd accepts articles with a valid or an unparsable Expires"
ok sh -c "inhist() { '$bin/expire/grephistory' \"<\$1@live.test>\" >/dev/null 2>&1; }; inhist good-expires && inhist good-badexpires"

# --- Event loop: control while busy, child exits --------------------------------

# ctlinnd must get an answer while a large feed is being read: the control
# channel is served on every pass of the main loop.
busy=$tmp/busybatch
: >"$busy"
for i in $(seq 1 3000); do
    art="Path: live.test!not-for-mail
From: tester@live.test
Newsgroups: test.live
Subject: busy $i
Message-ID: <busy$i@live.test>
Date: $(date -u '+%a, %d %b %Y %H:%M:%S +0000')

Busy body $i.
"
    printf '#! rnews %d\n%s' "${#art}" "$art" >>"$busy"
done
"$bin/frontends/rnews" <"$busy" >>"$tmp/rnews.log" 2>&1 &
rnews_pid=$!
sleep 0.2
label="ctlinnd answers while a feed is busy"
ok "$bin/frontends/ctlinnd" -s -t 10 mode
wait "$rnews_pid"
ctl flush ''
label="the busy feed is accepted in full"
ok sh -c "'$bin/expire/grephistory' '<busy1@live.test>' >/dev/null && '$bin/expire/grephistory' '<busy3000@live.test>' >/dev/null"

# An exploder whose program exits is noticed (SIGCHLD, PROCscan) and
# started again for the next text sent to it.
pkill -f "$tmp/explode.sh" 2>/dev/null
sleep 1
ctl send explode "after child exit"
sleep 1
label="an exploder is restarted after its child exits"
ok grep -q 'after child exit' "$tmp/explode.out"
label="innd survives the child exit"
ok "$bin/frontends/ctlinnd" -s -t 10 mode

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

# --- Embedded Perl and Python filters -------------------------------------------
#
# Only on a build with INN_PERL_FILTERS or INN_PYTHON_FILTERS.  innd loads
# filter_innd.pl (with startup_innd.pl) and filter_innd.py from pathfilter
# when it starts.  Two runs, so each rejection has one source: the spam
# filters from tests/data (one rejection reason per rule) in one language,
# the shipped sample, which accepts everything, in the other; then swapped.

do_perl=false
do_python=false
grep -q '^#define DO_PERL 1' "$build/include/config.h" && do_perl=true
grep -q '^#define DO_PYTHON 1' "$build/include/config.h" && do_python=true
filterdir=$(sed -n 's/^pathfilter:[[:space:]]*//p' "$INNCONF")

# filt_ihave ID GROUPS SUBJECT PATH FROM: offer one article by IHAVE; innd's
# reply goes to $tmp/filt-ID.out.  An empty FROM leaves the field out.
filt_ihave() {
    { printf 'IHAVE <%s@live.test>\r\n' "$1"
      { printf 'Path: %s\nNewsgroups: %s\nSubject: %s\n' "$4" "$2" "$3"
        [ -n "$5" ] && printf 'From: %s\n' "$5"
        printf 'Message-ID: <%s@live.test>\nDate: %s\n\nBody.\n.\n' "$1" \
            "$(date -u '+%a, %d %b %Y %H:%M:%S +0000')"
      } | sed 's/$/\r/'
      printf 'QUIT\r\n'; } | nc -w 10 127.0.0.1 "$port" >"$tmp/filt-$1.out" 2>&1
}
# filt_start LABEL: start innd and wait until it answers ctlinnd.
filt_start() {
    "$bin/innd/innd" -f >>"$tmp/innd.out" 2>&1 &
    innd_pid=$!
    local up=false
    for _ in $(seq 1 50); do
        if "$bin/frontends/ctlinnd" -s mode >/dev/null 2>&1; then
            up=true
            break
        fi
        kill -0 "$innd_pid" 2>/dev/null || break
        sleep 0.2
    done
    label="$1"
    ok $up
}
filt_stop() {
    "$bin/frontends/ctlinnd" -s shutdown "filters done" >/dev/null 2>&1
    for _ in $(seq 1 50); do
        kill -0 "$innd_pid" 2>/dev/null || break
        sleep 0.2
    done
    wait "$innd_pid" 2>/dev/null
    innd_pid=
}
# filt_checks LANG P: the spam filter's checks, articles prefixed P.
filt_checks() {
    local lang=$1 p=$2
    filt_ihave "$p-clean" test.live 'A clean article' 'live.test!not-for-mail' tester@live.test
    filt_ihave "$p-xpost" test.live,a.one,a.two,a.three,a.four,a.five 'Hello' 'live.test!not-for-mail' tester@live.test
    filt_ihave "$p-bin" test.live 'file.zip yEnc (1/3)' 'live.test!not-for-mail' tester@live.test
    filt_ihave "$p-binok" alt.binaries.test,test.live 'file.zip yEnc (1/3)' 'live.test!not-for-mail' tester@live.test
    filt_ihave "$p-path" test.live 'Hello' 'live.test!badrelay.example!not-for-mail' tester@live.test
    label="$lang filter accepts a clean article"
    ok sh -c "grep -q '^235' '$tmp/filt-$p-clean.out' && '$bin/expire/grephistory' '<$p-clean@live.test>' >/dev/null 2>&1"
    label="$lang filter rejects cross-posting to 6 groups"
    ok grep -q '^437.*Excessive cross-posting is limited to 5 groups max' "$tmp/filt-$p-xpost.out"
    label="$lang filter rejects yEnc outside binaries groups"
    ok grep -q '^437.*Binary attachments are not allowed in text hierarchies' "$tmp/filt-$p-bin.out"
    label="$lang filter accepts yEnc in a binaries group"
    ok grep -q '^235' "$tmp/filt-$p-binok.out"
    label="$lang filter rejects a blocked host in Path"
    ok grep -q '^437.*Article injected via a blocked transit host' "$tmp/filt-$p-path.out"
}
# filt_logged LANG P: after innd has stopped (it buffers the news log).
filt_logged() {
    label="$1 filter rejection is logged in the news log"
    ok grep -q "<$2-xpost@live.test> 437 Excessive cross-posting" "$tmp/log/news"
}
# filt_install PERL PYTHON: put the given filter files (or none) in place.
filt_install() {
    mkdir -p "$filterdir"
    rm -f "$filterdir"/filter_innd.pl "$filterdir"/startup_innd.pl \
        "$filterdir"/filter_innd.py
    if [ -n "$1" ]; then
        cp "$root/samples/startup_innd.pl" "$filterdir/startup_innd.pl"
        cp "$1" "$filterdir/filter_innd.pl"
    fi
    [ -n "$2" ] && cp "$2" "$filterdir/filter_innd.py"
    return 0
}

if $do_perl; then
    filt_install "$root/tests/data/filter_innd_spam.pl" \
        "$($do_python && echo "$root/samples/filter_innd.py")"
    filt_start "innd starts with the Perl spam filter"
    label="ctlinnd mode reports Perl filtering enabled"
    ok sh -c "'$bin/frontends/ctlinnd' mode | grep -q 'Perl filtering enabled'"
    if $do_python; then
        label="ctlinnd mode reports the Python sample enabled"
        ok sh -c "'$bin/frontends/ctlinnd' mode | grep -q 'Python filtering enabled'"
    fi
    filt_checks Perl pl
    # Off, the same article that was refused is accepted.
    label="ctlinnd perl n turns the Perl filter off"
    ok sh -c "'$bin/frontends/ctlinnd' -s perl n && '$bin/frontends/ctlinnd' mode | grep -q 'Perl filtering disabled'"
    filt_ihave pl-xpost-off test.live,a.one,a.two,a.three,a.four,a.five 'Hello' 'live.test!not-for-mail' tester@live.test
    label="with Perl filtering off, the cross-post is accepted"
    ok grep -q '^235' "$tmp/filt-pl-xpost-off.out"
    filt_stop
    filt_logged Perl pl
fi
if $do_python; then
    filt_install "$($do_perl && echo "$root/samples/filter_innd.pl")" \
        "$root/tests/data/filter_innd_spam.py"
    filt_start "innd starts with the Python spam filter"
    label="ctlinnd mode reports Python filtering enabled"
    ok sh -c "'$bin/frontends/ctlinnd' mode | grep -q 'Python filtering enabled'"
    if $do_perl; then
        label="ctlinnd mode reports the Perl sample enabled"
        ok sh -c "'$bin/frontends/ctlinnd' mode | grep -q 'Perl filtering enabled'"
    fi
    filt_checks Python py
    label="ctlinnd python n turns the Python filter off"
    ok sh -c "'$bin/frontends/ctlinnd' -s python n && '$bin/frontends/ctlinnd' mode | grep -q 'Python filtering disabled'"
    filt_ihave py-xpost-off test.live,a.one,a.two,a.three,a.four,a.five 'Hello' 'live.test!not-for-mail' tester@live.test
    label="with Python filtering off, the cross-post is accepted"
    ok grep -q '^235' "$tmp/filt-py-xpost-off.out"
    filt_stop
    filt_logged Python py
fi
filt_install "" ""

# --- xrefslave mode -----------------------------------------------------------
#
# A slave server files each article under the numbers in its Xref header
# field (ARTxrefslave) instead of assigning its own.  Restart innd so.

# logipaddr false too: the news log then names the sender of a refused
# article by the first host in its Path (ARTlogreject).
sed -i.bak -e 's|^xrefslave:.*|xrefslave: true|' \
    -e 's|^logipaddr:.*|logipaddr: false|' "$INNCONF" && rm -f "$INNCONF.bak"
"$bin/innd/innd" -f >>"$tmp/innd.out" 2>&1 &
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
label="innd restarts in xrefslave mode"
ok sh -c "$up && '$bin/frontends/ctlinnd' mode 2>/dev/null | grep -qi 'slave'"

send_art slave-500 test.live 'Xref: master.test test.live:50000'
# A malformed entry second: tradspool takes the article's path from the
# first entry, and refuses the article if that one is malformed.
send_art slave-501 test.live 'Xref: master.test test.live:50001 bad-entry'
send_art slave-noxref test.live
send_art slave-nogroup test.live 'Xref: master.test no.such.group:7'
send_art slave-noentries test.live 'Xref: master.test'
# Refused while it is parsed, so logged by ARTlogreject.
ihave_nul slave-nul@live.test ihave-slavenul.out 'Subject: nul\0here\r\n\r\nBody.\r\n'
ctl flush ''
sleep 1
ov=$("$bin/storage/tdx-util" -o -n test.live 2>/dev/null)
label="xrefslave files an article under its Xref number"
ok sh -c "printf '%s\n' \"\$1\" | grep -q '^50000	.*slave-500'" _ "$ov"
label="xrefslave skips a malformed Xref entry, files the rest"
ok sh -c "printf '%s\n' \"\$1\" | grep -q '^50001	.*slave-501'" _ "$ov"
label="xrefslave rejects an article without Xref"
ok grep -q 'Xref header field required in xrefslave mode' "$tmp/log/news"
label="without logipaddr, a refusal is logged under the first Path host"
ok grep -q ' - live.test <slave-nul@live.test> 437 Nul character in header' "$tmp/log/news"
label="xrefslave rejects an Xref with no known group or no entries"
ok sh -c "[ \$(grep -c 'Xref header field \"master.test[^\"]*\" invalid in xrefslave mode' '$tmp/log/news') -ge 2 ]"

# SIGHUP, like SIGTERM, makes innd exit cleanly from its main loop.  The
# select() loop only looks at the signal after a pass with activity (an
# interrupted select() goes straight back to waiting), so give it some.
kill -HUP "$innd_pid"
sleep 0.5
"$bin/frontends/ctlinnd" -s -t 5 mode >/dev/null 2>&1
for _ in $(seq 1 50); do
    kill -0 "$innd_pid" 2>/dev/null || break
    sleep 0.2
done
label="SIGHUP stops innd cleanly"
if kill -0 "$innd_pid" 2>/dev/null; then
    res "$label" FAIL
else
    wait "$innd_pid" 2>/dev/null
    status=$?
    innd_pid=
    ok test "$status" = 0
fi

if [ $fail = 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
    exit 1
fi
