#! /bin/sh
#
# Test suite for buffchan: what it writes, where, and when.  Written before
# its move to Boost.Asio, against the stdio version, so both must pass.
#
# Most tests feed buffchan through a FIFO so it stays running between
# writes; "when" is then whether the data is on disk yet.

count=1
printcount() {
    echo "$1 $count $2"
    count=$(expr $count + 1)
}
check() {
    if eval "$1"; then
        printcount "ok" "- $2"
    else
        printcount "not ok" "- $2"
        echo "#   failed: $1" >&2
    fi
}

# Wait up to five seconds for a shell condition.
waitfor() {
    n=0
    while [ $n -lt 50 ]; do
        eval "$1" && return 0
        sleep 0.1
        n=$(expr $n + 1)
    done
    return 1
}

# Octal permission bits of a file.
mode() {
    if stat -c %a "$1" >/dev/null 2>&1; then
        stat -c %a "$1"
    else
        stat -f %Lp "$1"
    fi
}

buffchan=$(pwd)/../backends/buffchan
if [ ! -x "$buffchan" ]; then
    echo "Could not find buffchan" >&2
    exit 1
fi
INNCONF=$(pwd)/data/etc/inn.conf
export INNCONF

tmp=$(pwd)/buffchan.tmp
rm -rf "$tmp"
mkdir "$tmp"
trap 'rm -rf "$tmp"' 0

# Start buffchan in the background on a FIFO, with the given options and
# -r -d $tmp/out; open the FIFO for writing on descriptor 9.
start() {
    rm -rf "$tmp/out" "$tmp/fifo" "$tmp/err"
    mkdir "$tmp/out"
    mkfifo "$tmp/fifo"
    "$buffchan" -r -d "$tmp/out" "$@" <"$tmp/fifo" 2>"$tmp/err" &
    pid=$!
    exec 9>"$tmp/fifo"
}
send() {
    printf '%s\n' "$1" >&9
}
# Close the FIFO and wait for buffchan to exit; its status is in $status.
finish() {
    exec 9>&-
    wait $pid
    status=$?
}
# Run buffchan to completion on the given input.
run() {
    input=$1
    shift
    rm -rf "$tmp/out" "$tmp/err"
    mkdir "$tmp/out"
    printf '%s' "$input" | "$buffchan" -r -d "$tmp/out" "$@" 2>"$tmp/err"
    status=$?
}

echo 35

# One file per site, lines in order, the leading field only.
run 'first siteA
second siteB siteA
third siteA
'
check '[ $status = 0 ]' 'exits 0 at end of input'
check '[ "$(cat $tmp/out/siteA)" = "first
second
third" ]' 'appends each line to its sites in order'
check '[ "$(cat $tmp/out/siteB)" = second ]' 'a line goes to every listed site'
check '[ "$(mode $tmp/out/siteA)" = 664 ]' 'a closed file is mode 0664'

# -f: the number of leading fields kept.
run 'a b siteA siteB
' -f 2
check '[ "$(cat $tmp/out/siteA)" = "a b" ] && [ "$(cat $tmp/out/siteB)" = "a b" ]' \
    '-f 2 keeps two leading fields'

# Extra spaces between sites, and a line with no sites.
run 'x  siteA   siteB
nosites
' -f 1
check '[ "$(cat $tmp/out/siteA)" = x ] && [ "$(cat $tmp/out/siteB)" = x ] && [ "$(ls $tmp/out | wc -l)" -eq 2 ]' \
    'skips repeated spaces; a line without sites writes nothing'

# Site names are case-sensitive.  (The stdio version compared names with
# strcasecmp, but only after an exact match of the first character and a
# case-sensitive hash, so in practice they always were.)  A command shows
# it, since on a case-insensitive file system two sites share a file.
start
send 'one SiteA'
send '!flush siteA'
send '!flush sitea'
sleep 0.3
[ -s $tmp/out/SiteA ] && early=yes || early=no
send '!flush SiteA'
check '[ $early = no ] && waitfor "[ \"\$(cat $tmp/out/SiteA)\" = one ]"' \
    'site names are case-sensitive'
finish

# -s: the file name format.
run 'x siteA
' -s 'out.%s'
check '[ "$(cat $tmp/out/out.siteA)" = x ]' '-s formats the file name'

# -m: the site-name map; !readmap rereads it for sites seen afterwards.
printf '# comment\n\nsiteA:mapped\n' >"$tmp/map"
start -m "$tmp/map"
send 'one siteA'
send 'two siteB'
waitfor "[ -e $tmp/out/siteB ]"
printf 'siteB:mappedB\n' >"$tmp/map"
send '!readmap'
send 'three siteB'
send 'four siteC'
finish
check '[ "$(cat $tmp/out/mapped)" = one ]' '-m maps a site to a file name'
check '[ "$(cat $tmp/out/siteB)" = "two
three" ] && [ ! -e $tmp/out/mappedB ]' 'a known site keeps its file after !readmap'
check '[ "$(cat $tmp/out/siteC)" = four ]' 'an unmapped site uses its own name'

# Commands: !! starts a data line; unknown commands are warned about.
run '!!bang siteA
!bogus
!begin
'
check '[ "$(cat $tmp/out/siteA)" = "!bang" ]' '!! starts a data line beginning with !'
check 'grep -q "unknown message bogus" $tmp/err && ! grep -q begin $tmp/err' \
    'warns about an unknown command, not about !begin'

# A partial last line, without its newline, is not written.
run 'whole siteA
partial siteA'
check '[ "$(cat $tmp/out/siteA)" = whole ]' 'drops a partial last line'

# A line of 32 KiB or more is skipped with a warning.
long=$(head -c 40000 /dev/zero | tr '\0' x)
run "before siteA
$long siteA
after siteA
"
check '[ "$(cat $tmp/out/siteA)" = "before
after" ] && grep -q "long line" $tmp/err' 'skips a line of 32 KiB or more'

# Buffered by default: nothing is on disk until a flush.
start
send 'one siteA'
check 'waitfor "[ -e $tmp/out/siteA ]" && sleep 0.3 && [ ! -s $tmp/out/siteA ]' \
    'output is buffered by default'
check '[ "$(mode $tmp/out/siteA)" = 444 ]' 'an open file is mode 0444'
send '!flush siteA'
check 'waitfor "[ \"\$(cat $tmp/out/siteA)\" = one ]"' '!flush SITE writes the file out'
send 'two siteB'
send 'three siteA'
send '!flush'
check 'waitfor "[ -s $tmp/out/siteB ] && [ \"\$(cat $tmp/out/siteA)\" = \"one
three\" ]"' '!flush writes all files out'
send '!flush nosuch'
send '!drop siteB'
check 'waitfor "[ \"\$(mode $tmp/out/siteB)\" = 664 ]"' '!drop SITE closes the file'
send '!drop nosuch'
check 'waitfor "grep -q \"drop nosuch unknown site\" $tmp/err"' \
    '!drop of an unknown site warns'
send 'four siteB'
send '!drop'
check 'waitfor "[ \"\$(cat $tmp/out/siteB)\" = \"two
four\" ] && [ \"\$(mode $tmp/out/siteA)\" = 664 ]"' \
    'a dropped site reopens on its next line; !drop closes all'
finish
check '! grep -q nosuch.*flush $tmp/err && [ "$(cat $tmp/out/siteB)" = "two
four" ]' '!flush of an unknown site is silent'

# -l: flush every N lines.
start -l 2
send 'one siteA'
send 'two siteA'
check 'waitfor "[ \"\$(cat $tmp/out/siteA)\" = \"one
two\" ]"' '-l 2 flushes after two lines'
finish

# -c: close and reopen every N lines.
start -c 1
send 'one siteA'
check 'waitfor "[ \"\$(cat $tmp/out/siteA)\" = one ]"' '-c 1 closes after each line'
finish

# -u: unbuffered.
start -u
send 'one siteA'
check 'waitfor "[ \"\$(cat $tmp/out/siteA)\" = one ]"' '-u writes at once'
finish

# -b: buffered, as the default.
start -b
send 'one siteA'
check 'waitfor "[ -e $tmp/out/siteA ]" && sleep 0.3 && [ ! -s $tmp/out/siteA ]' '-b buffers'
finish

# -L: flush a file once N seconds have passed since its last flush; checked
# no later than the site's next line.
start -L 1
send 'one siteA'
sleep 2
send 'two siteA'
check 'waitfor "[ \"\$(cat $tmp/out/siteA)\" = \"one
two\" ]"' '-L 1 flushes after a second'
finish

# -C: close and reopen once N seconds have passed.
start -C 1
send 'one siteA'
sleep 2
send 'two siteA'
check 'waitfor "[ \"\$(cat $tmp/out/siteA)\" = \"one
two\" ]"' '-C 1 closes after a second'
finish

# The time limits act on an idle site too, with no further input: each
# site has its own timer.
start -L 1
send 'one siteA'
check 'waitfor "[ \"\$(cat $tmp/out/siteA)\" = one ]"' '-L 1 flushes an idle site'
finish
start -C 1
send 'one siteA'
check 'waitfor "[ \"\$(cat $tmp/out/siteA)\" = one ]"' '-C 1 closes an idle site'
check '[ "$(mode $tmp/out/siteA)" = 444 ]' '-C reopens the file after closing it'
finish

# -p: the PID file; SIGTERM closes the files and exits 0.  The stdio
# version notices the signal only when its read returns (reads restart
# after a signal), so the FIFO is closed after the signal is sent.
start -p "$tmp/pid"
send 'one siteA'
waitfor "[ -e $tmp/out/siteA ]"
check '[ "$(cat $tmp/pid)" = "$pid" ]' '-p writes the PID'
kill -TERM $pid
sleep 0.3
exec 9>&-
wait $pid
status=$?
check '[ $status = 0 ] && [ "$(cat $tmp/out/siteA)" = one ] && [ "$(mode $tmp/out/siteA)" = 664 ]' \
    'SIGTERM closes the files and exits 0'

# A bad option is a usage error.
"$buffchan" -r -Z </dev/null 2>"$tmp/err"
check '[ $? != 0 ] && grep -q "usage error" $tmp/err' 'a bad option is a usage error'
