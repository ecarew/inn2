#!/bin/bash
#
#  Checks of what INN does when started as root, which the normal checks
#  (run as the tester) cannot reach.  Run by hand with sudo after the normal
#  checks pass, and record the output in the commit:
#
#      sudo support/root-checks.sh [build-dir]     (default: build-live)
#
#  Use a build configured with INN_RUNAS_USER/INN_RUNAS_GROUP set to the
#  tester's account (build-live), so "dropping to the news user" means
#  becoming the tester.  Everything is compiled and run in a temporary
#  directory, so no root-owned files are left in the tree or the build.
#  Prints one line per check and a RESULT line, like support/verify-cxx.sh.

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/${1:-build-live}
fail=0

res() {
    printf '  %-50s %s\n' "$1" "$2"
    [[ "$2" == PASS* ]] || fail=1
}

if [ "$(id -u)" != 0 ]; then
    echo "must be run as root: sudo $0 $*" >&2
    exit 2
fi
[ -f "$build/CMakeCache.txt" ] || { echo "no build in $build" >&2; exit 2; }
runas=$(sed -n 's/^INN_RUNAS_USER:STRING=//p' "$build/CMakeCache.txt")
if [ -z "$runas" ] || [ "$runas" = root ]; then
    echo "$build is not configured with INN_RUNAS_USER (use build-live)" >&2
    exit 2
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/root-checks.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# --- lib/newsuser: the shared privilege drop ----------------------------------

# Compile the C test as C, then link with the C++ driver, which adds the
# platform's C++ runtime for the converted libinn (libc++ on macOS,
# libstdc++ on Linux).
cflags=(-I"$build/include" -I"$root/include" -I"$root/tests" -DTESTING)
if cc "${cflags[@]}" -c -o "$tmp/newsuser-t.o" "$root/tests/lib/newsuser-t.c" \
      >"$tmp/cc.log" 2>&1 \
   && cc "${cflags[@]}" -c -o "$tmp/basic.o" "$root/tests/tap/basic.c" \
      >>"$tmp/cc.log" 2>&1 \
   && c++ -o "$tmp/newsuser.t" "$tmp/newsuser-t.o" "$tmp/basic.o" \
      "$build/lib/libinn.a" >>"$tmp/cc.log" 2>&1; then
    out=$("$tmp/newsuser.t" 2>&1)
    if echo "$out" | grep -q '^not ok' || ! echo "$out" | grep -q '^ok'; then
        res "lib/newsuser test" FAIL
        echo "$out" | sed 's/^/    /'
    else
        res "lib/newsuser test" "PASS ($(echo "$out" | grep -c '^ok') checks)"
        echo "$out" | sed 's/^/    /'
    fi
else
    res "lib/newsuser test" "FAIL (build)"
    sed 's/^/    /' "$tmp/cc.log" | tail -10
fi

# --- A scratch installation owned by the news user -----------------------------

# The programs below run as root and must drop to (or refuse in favour of)
# the news user, so the scratch tree is owned by that user, and the
# directories above it must be traversable.
chmod 755 "$tmp"
runasgroup=$(sed -n 's/^INN_RUNAS_GROUP:STRING=//p' "$build/CMakeCache.txt")
news_ids=$(perl -e '@p = getpwnam(shift); @g = getgrnam(shift);
                    print "$p[2]:$g[2]"' "$runas" "$runasgroup")
s=$tmp/inn
mkdir -p "$s"/{etc,db,run,log,tmp} \
         "$s"/spool/{articles,overview,incoming,outgoing,archive}
export INNCONF=$s/etc/inn.conf
sed -e "s|/usr/local/news|$s|g" -e "s|^pathhistory:.*|pathhistory: $s/db|" \
    -e "s|^ovmethod:.*|ovmethod: ovsqlite|" \
    -e "s|^nnrpdpostport:.*|nnrpdpostport: 1|" \
    "$root/samples/inn.conf" >"$INNCONF"
cat >>"$INNCONF" <<EOF
runasuser:  $runas
runasgroup: $runasgroup
server:     127.0.0.1
domain:     root-checks.test
EOF
printf 'method tradspool {\n    newsgroups: *\n    class: 0\n}\n' \
    >"$s/etc/storage.conf"
printf 'auth "all" { hosts: "*" default: "<local>" }\n' >"$s/etc/readers.conf"
printf 'access "all" { users: "*" newsgroups: "*" }\n' >>"$s/etc/readers.conf"
printf 'test.root 0000000000 0000000001 y\n' >"$s/db/active"
printf 'test.root\troot checks\n' >"$s/db/newsgroups"
: >"$s/db/history"
"$build/expire/makedbz" -i -o >/dev/null 2>&1
chown -R "$runas:$runasgroup" "$s"

# owner FILE: "uid:gid" of FILE.  ps_ids PID: "uid:gid" of a process.
owner() { perl -e '@s = stat shift or exit 1; print "$s[4]:$s[5]"' "$1"; }
ps_ids() { ps -o uid= -o rgid= -p "$1" | awk '{print $1 ":" $2}'; }
owned_by_news() {
    local label=$1 file=$2 ids
    ids=$(owner "$file") || { res "$label" "FAIL (no $file)"; return; }
    if [ "$ids" = "$news_ids" ]; then
        res "$label" PASS
    else
        res "$label" "FAIL (owner $ids, want $news_ids)"
    fi
}

# --- rnews as root: drops user, group and supplementary groups ---------------

# With no server to talk to (nnrpdpostport 1), rnews spools the batch to
# the incoming directory; the file shows which user and group it ran as.
art=$'Path: root-checks.test!not-for-mail\nFrom: t@root-checks.test\nNewsgroups: test.root\nSubject: root checks\nMessage-ID: <rnews@root-checks.test>\nDate: Thu, 29 Oct 2026 00:00:00 +0000\n\nBody.\n'
printf '#! rnews %d\n%s' "${#art}" "$art" \
    | "$build/frontends/rnews" >"$tmp/rnews.out" 2>&1
spooled=$(ls "$s/spool/incoming"/* 2>/dev/null | head -1)
owned_by_news "rnews as root spools as the news user and group" \
    "${spooled:-$s/spool/incoming/(nothing spooled)}"

# --- ovsqlite-server as root ---------------------------------------------------

"$build/storage/ovsqlite-server" -d >"$tmp/ovsqlite.out" 2>&1 &
ovs=$!
for _ in $(seq 1 50); do
    [ -e "$s/spool/overview/ovsqlite.db" ] && break
    sleep 0.1
done
ovs_ids=$(ps_ids "$ovs")
kill "$ovs" 2>/dev/null; wait "$ovs" 2>/dev/null
owned_by_news "ovsqlite-server as root creates its database as news" \
    "$s/spool/overview/ovsqlite.db"
if [ "$ovs_ids" = "$news_ids" ]; then
    res "ovsqlite-server as root runs as the news user" PASS
else
    res "ovsqlite-server as root runs as the news user" \
        "FAIL (${ovs_ids:-exited}, want $news_ids)"
    sed 's/^/    /' "$tmp/ovsqlite.out" | tail -5
fi

# --- innfeed as root ---------------------------------------------------------

# innfeed records its PID in pathrun at startup.  Its configuration names
# only a backlog directory and no peers, so it has nothing to feed; stop it
# once the PID file appears.
mkdir -p "$s/spool/innfeed"
printf 'backlog-directory: %s\n' "$s/spool/innfeed" >"$s/etc/innfeed.conf"
chown -R "$runas:$runasgroup" "$s/etc/innfeed.conf" "$s/spool/innfeed"
"$build/innfeed/innfeed" </dev/null >"$tmp/innfeed.out" 2>&1 &
inf=$!
for _ in $(seq 1 50); do
    [ -e "$s/run/innfeed.pid" ] && break
    sleep 0.1
done
kill "$inf" 2>/dev/null; wait "$inf" 2>/dev/null
owned_by_news "innfeed as root writes its PID file as news" \
    "$s/run/innfeed.pid"
[ -e "$s/run/innfeed.pid" ] || sed 's/^/    /' "$tmp/innfeed.out" | tail -5

# --- nnrpd as root without -D (as inetd would start it) -----------------------

# A Perl listener accepts one connection and execs nnrpd on it, as root,
# with the socket on standard input and output, the way inetd does.  While
# the client holds the session open, check which user nnrpd runs as.
port=11129
perl -MIO::Socket::INET -e '
    my $l = IO::Socket::INET->new(LocalAddr => "127.0.0.1", LocalPort => shift,
                                  Listen => 1, ReuseAddr => 1) or die "$!";
    my $c = $l->accept or die "$!";
    open(STDIN, "<&", $c) and open(STDOUT, ">&", $c) or die "$!";
    exec @ARGV or die "$!";' "$port" "$build/nnrpd/nnrpd" -n \
    2>"$tmp/nnrpd.err" &
nnrpd_pid=$!
sleep 1
{ sleep 2; printf 'QUIT\r\n'; } | nc 127.0.0.1 "$port" >"$tmp/nnrpd.out" 2>&1 &
client=$!
sleep 1
nn_ids=$(ps_ids "$nnrpd_pid")
wait "$client" 2>/dev/null
kill "$nnrpd_pid" 2>/dev/null; wait "$nnrpd_pid" 2>/dev/null
if [ "$nn_ids" = "$news_ids" ] && grep -q '^20[01] ' "$tmp/nnrpd.out"; then
    res "nnrpd as root without -D serves as the news user" PASS
else
    res "nnrpd as root without -D serves as the news user" \
        "FAIL (${nn_ids:-exited}, want $news_ids)"
    sed 's/^/    /' "$tmp/nnrpd.out" "$tmp/nnrpd.err" | tail -5
fi

# --- Programs that write INN's data refuse root --------------------------------

for p in backends/innxmit backends/batcher backends/buffchan \
         backends/overchan backends/archive expire/fastrm \
         expire/prunehistory backends/shrinkfile backends/innxbatch; do
    out=$("$build/$p" </dev/null 2>&1)
    if [ $? -ne 0 ] && echo "$out" | grep -q 'must not be run as root'; then
        res "$(basename "$p") refuses root" PASS
    else
        res "$(basename "$p") refuses root" "FAIL ($(echo "$out" | head -1))"
    fi
done

# --- Scripts that write INN's data refuse root ----------------------------------

# Line 2 of each script is replaced by fixscript with code that loads
# innshellvars (shell) or INN::Config (Perl).  A minimal stand-in that sets
# only the news user is enough to reach the check, which comes right after
# it (in a BEGIN block in Perl, so missing modules cannot get in first).
for p in scripts/news.daily scripts/scanlogs scripts/innwatch \
         scripts/writelog scripts/tally.control backends/nntpsend \
         backends/send-ihave backends/sendxbatches control/controlbatch \
         backends/actsyncd expire/expirerm; do
    { sed -n 1p "$root/$p.in"; echo "NEWSUSER=$runas"
      sed 1,2d "$root/$p.in"; } >"$tmp/script"
    out=$(sh "$tmp/script" </dev/null 2>&1)
    if [ $? -ne 0 ] && echo "$out" | grep -q 'must not be run as root'; then
        res "$(basename "$p") refuses root" PASS
    else
        res "$(basename "$p") refuses root" "FAIL ($(echo "$out" | head -1))"
    fi
done
for p in backends/send-uucp control/controlchan control/perl-nocem \
         innfeed/procbatch scripts/innreport history/hissqlite/hissqlite-util \
         storage/ovsqlite/ovsqlite-util frontends/cnfsheadconf; do
    { echo "BEGIN { \$INN::Config::runasuser = '$runas'; }"
      sed 1,2d "$root/$p.in"; } >"$tmp/script"
    out=$(perl "$tmp/script" </dev/null 2>&1)
    if [ $? -ne 0 ] && echo "$out" | grep -q 'must not be run as root'; then
        res "$(basename "$p") refuses root" PASS
    else
        res "$(basename "$p") refuses root" "FAIL ($(echo "$out" | head -1))"
    fi
done

if [ $fail = 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
    exit 1
fi
