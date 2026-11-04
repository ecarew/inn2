#!/bin/bash
#
#  Performance comparison (doc/spec/performance-comparison.md): run the
#  day's workload W1-W6 on several builds, alternating, and print one line
#  per measurement.  As an ordinary user, in scratch installations on high
#  ports; never as root.
#
#  Usage: support/perf/run-compare.sh [-n reps] [-s scenarios] feed name=dir ...
#    feed       articles from genarticles (the same file for every build)
#    name=dir   a build to compare: its name in the report, and the
#               directory holding innd/, nnrpd/, innfeed/, frontends/,
#               expire/ (a CMake build directory, or a Make source tree)
#    -n reps    repetitions per build (default 5)
#    -s list    scenarios, comma-separated (default W1,W2,W3,W4,W5,W6)
#
#  PERF_TOOLS names the directory with genarticles, feeder and readers.
#  PERF_KEEP=1 keeps the last scratch installation.
#  PERF_LEAKS=1 is the leak pass (section 5.4): programs run with malloc
#    stack logging, the servers are checked with leaks(1) before they stop
#    and the short programs with leaks --atExit; "<prog>_leaks" and
#    "<prog>_leaked_bytes" lines replace the time and memory ones.  Never
#    timed: stack logging slows programs down.
#  PERF_COVDIR=dir is the coverage pass (section 5.5): every process of
#    build NAME writes its clang profile to dir/NAME/.
#
#  Output: "build rep scenario measure value" lines.  A scenario that
#  cannot finish correctly prints "build rep scenario FAILED reason" and
#  the later scenarios of that run are skipped (they depend on it).

set -u
reps=5
scenarios=W1,W2,W3,W4,W5,W6
while getopts n:s: o; do
    case $o in
    n) reps=$OPTARG ;;
    s) scenarios=$OPTARG ;;
    *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 2 ] || { sed -n '7,20p' "$0"; exit 2; }
feed=$1; shift
tools=${PERF_TOOLS:?set PERF_TOOLS to the directory with genarticles, feeder and readers}
limit=${PERF_TIMEOUT:-900}       # seconds any one step may take
readers_n=20
readers_articles=250             # per session: 20 x 250 = 5,000
seed=0x1e57

narts=$(grep -c '^#! ' "$feed")
# Group placements: an article in N groups has N overview lines and N
# tradspool files.
placements=$(awk '/^#! / { n += $3 } END { print n }' "$feed")
groups=$("$tools/genarticles" -a | awk '{ print $1 }')

# One inn.conf for every build (P4): the first build's sample, with the
# sample's prefix (its pathnews) replaced by each scratch directory.
first_dir=${1#*=}
sample=$first_dir/samples/inn.conf
sample_prefix=$(awk '$1 == "pathnews:" { print $2 }' "$sample")

out() { printf '%s %s %s %s %s\n' "$build" "$rep" "$1" "$2" "$3"; }
failed() { out "$1" FAILED "$2"; }
now() { perl -MTime::HiRes=time -e 'printf "%.3f\n", time'; }
elapsed() { awk -v a="$1" -v b="$2" 'BEGIN { printf "%.3f", b - a }'; }

# cpu_s <pid>: user+system CPU seconds so far, from ps ("M:SS.ss").
cpu_s() {
    ps -o time= -p "$1" | awk -F: '{ s = 0; for (i = 1; i <= NF; i++) s = s * 60 + $i; print s }'
}

# rusage <prog> <scenario> <file>: the /usr/bin/time -l lines (none in
# the leak pass).
rusage() {
    [ -s "$3" ] || return 0
    awk -v b="$build" -v r="$rep" -v s="$2" -v p="$1" '
        / real / { printf "%s %s %s %s_user_s %s\n", b, r, s, p, $3
                   printf "%s %s %s %s_sys_s %s\n", b, r, s, p, $5 }
        /maximum resident set size/ {
                   printf "%s %s %s %s_peak_rss_bytes %s\n", b, r, s, p, $1 }
        /peak memory footprint/ {
                   printf "%s %s %s %s_peak_footprint_bytes %s\n", b, r, s, p, $1 }
    ' "$3"
}

leaks_mode=${PERF_LEAKS:+1}
# measured <file>: the command prefix that measures a program: rusage into
# <file>, or in the leak pass leaks --atExit (its report goes to the
# program's standard output).
measured() {
    if [ -n "$leaks_mode" ]; then
        measured_cmd=(env MallocStackLogging=1 leaks --atExit --)
    else
        measured_cmd=(/usr/bin/time -l -o "$1")
    fi
}
# leakcount <prog> <scenario> <file>: the leaks(1) summary line, as
# "<prog>_leaks N" and "<prog>_leaked_bytes B".
leakcount() {
    [ -n "$leaks_mode" ] || return 0
    local line
    line=$(grep -E '^Process [0-9]+: [0-9]+ leaks? for [0-9]+ total leaked bytes' "$3" | tail -1)
    if [ -z "$line" ]; then
        failed "$2" "$1: no leaks(1) report ($(tail -1 "$3"))"; return 0
    fi
    out "$2" "$1_leaks" "$(echo "$line" | awk '{ print $3 }')"
    out "$2" "$1_leaked_bytes" "$(echo "$line" | awk '{ print $6 }')"
}
# server_leaks <prog> <scenario> <pid> <file>: leaks(1) on a running server.
server_leaks() {
    [ -n "$leaks_mode" ] || return 0
    leaks "$3" >"$4" 2>&1
    leakcount "$1" "$2" "$4"
}
# ok_status: in the leak pass leaks(1) exits 1 when it finds leaks, so the
# program's own status is lost; the checks after each step still apply.
ok_status() { [ "$1" -eq 0 ] || [ -n "$leaks_mode" ]; }

# setup <bin> <dir> <port> <pathhost>: a fresh scratch installation.
setup() {
    local bin=$1 d=$2 port=$3 host=$4
    rm -rf "$d"
    mkdir -p "$d"/{etc,db,run,log,tmp,http} \
             "$d"/spool/{articles,overview,incoming,outgoing,archive,innfeed}
    sed -e "s|$sample_prefix|$d|g" \
        -e "s|^pathhistory:.*|pathhistory: $d/db|" \
        -e "s|^pathhost:.*|pathhost: $host|" \
        -e "s|^port:.*|port: $port|" \
        -e "s|^noreader:.*|noreader: true|" \
        -e "s|^artcutoff:.*|artcutoff: 30|" "$sample" >"$d/etc/inn.conf"
    cat >>"$d/etc/inn.conf" <<EOF
runasuser:   $(id -un)
runasgroup:  $(id -gn)
bindaddress: 127.0.0.1
server:      127.0.0.1
domain:      perf.test
fromhost:    perf.test
EOF
    printf 'ME:*::\n' >"$d/etc/newsfeeds"
    cat >"$d/etc/incoming.conf" <<'EOF'
streaming: true
max-connections: 16
peer localhost {
    hostname: "127.0.0.1, localhost"
}
EOF
    printf 'method tradspool {\n    newsgroups: *\n    class: 0\n}\n' \
        >"$d/etc/storage.conf"
    cat >"$d/etc/readers.conf" <<'EOF'
auth "localhost" { hosts: "127.0.0.1" default: "<local>" }
access "localhost" { users: "<local>" newsgroups: "*" }
EOF
    {
        printf '%s 0000000000 0000000001 y\n' control control.cancel junk
        "$tools/genarticles" -a
    } >"$d/db/active"
    : >"$d/db/newsgroups"
    : >"$d/db/history"
    INNCONF=$d/etc/inn.conf "$bin/expire/makedbz" -i -o >"$d/makedbz.log" 2>&1
}

# start_innd <bin> <dir> [time-file]: innd in the foreground, under
# /usr/bin/time when a file is given; sets started_pid.
start_innd() {
    local bin=$1 d=$2
    if [ -n "${3:-}" ]; then
        env INNCONF=$d/etc/inn.conf ${leaks_mode:+MallocStackLogging=1} \
            /usr/bin/time -l -o "$3" "$bin/innd/innd" -f >"$d/innd.out" 2>&1 &
    else
        INNCONF=$d/etc/inn.conf "$bin/innd/innd" -f >"$d/innd.out" 2>&1 &
    fi
    started_pid=$!
    for _ in $(seq 1 100); do
        INNCONF=$d/etc/inn.conf "$bin/frontends/ctlinnd" -s mode >/dev/null 2>&1 && return 0
        kill -0 "$started_pid" 2>/dev/null || break
        sleep 0.1
    done
    # innd reports startup errors to syslog.
    start_error=$( { cat "$d/innd.out" "$d/log/errlog"
        log show --last 1m --style compact --predicate 'process == "innd"' 2>/dev/null; } \
        | tail -3 | tr '\n' ' ')
    return 1
}

stop_innd() {
    local bin=$1 d=$2 pid=$3
    INNCONF=$d/etc/inn.conf "$bin/frontends/ctlinnd" -s shutdown perf >/dev/null 2>&1
    for _ in $(seq 1 100); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
}

# The process under /usr/bin/time (by parent: innd and nnrpd rename
# themselves, so a match by name can miss).
innd_child() { pgrep -P "$1" || echo "$1"; }

ctl() { INNCONF=$main/etc/inn.conf "$bin/frontends/ctlinnd" -s "$@"; }

spool_files() { find "$1/spool/articles" -type f | wc -l | tr -d ' '; }

# --- Scenarios ----------------------------------------------------------------
# Each returns non-zero after printing FAILED.

w1() {
    local f=$main/w1.out c0
    c0=$(cpu_s "$innd")
    if ! timeout "$limit" "$tools/feeder" takethis 127.0.0.1 "$mport" "$feed" >"$f" 2>&1; then
        failed W1 "feeder: $(tail -1 "$f")"; return 1
    fi
    local accepted secs
    accepted=$(awk '$1 == "code_239" { print $2 }' "$f")
    if [ "${accepted:-0}" != "$narts" ]; then
        failed W1 "accepted ${accepted:-0} of $narts ($(grep code_ "$f" | tr '\n' ' '))"; return 1
    fi
    secs=$(awk '$1 == "seconds" { print $2 }' "$f")
    out W1 wall_s "$secs"
    out W1 articles_per_s "$(awk -v n="$narts" -v s="$secs" 'BEGIN { printf "%.0f", n / s }')"
    out W1 innd_cpu_s "$(elapsed "$c0" "$(cpu_s "$innd")")"
}

w2() {
    local f=$main/w2.out c0
    c0=$(cpu_s "$innd")
    if ! timeout "$limit" "$tools/feeder" check 127.0.0.1 "$mport" "$feed" >"$f" 2>&1; then
        failed W2 "feeder: $(tail -1 "$f")"; return 1
    fi
    local refused
    refused=$(awk '$1 == "code_438" { print $2 }' "$f")
    if [ "${refused:-0}" != "$narts" ]; then
        failed W2 "refused ${refused:-0} of $narts ($(grep code_ "$f" | tr '\n' ' '))"; return 1
    fi
    out W2 wall_s "$(awk '$1 == "seconds" { print $2 }' "$f")"
    out W2 innd_cpu_s "$(elapsed "$c0" "$(cpu_s "$innd")")"
}

# W3: innfeed streams every article to a peer innd (a second scratch
# server from the same build), from the channel lines innd writes for it.
w3() {
    local peer=$scratch/peer pport=$((mport + 1)) ppid t0 t1
    if ! setup "$bin" "$peer" "$pport" peer.perf.test; then
        failed W3 "peer: makedbz"; return 1
    fi
    if ! start_innd "$bin" "$peer"; then
        failed W3 "peer innd did not start: $start_error"; return 1
    fi
    ppid=$started_pid
    ctl flush perfpeer >/dev/null 2>&1
    local batch=$main/spool/outgoing/perfpeer
    if [ "$(wc -l <"$batch" | tr -d ' ')" != "$narts" ]; then
        failed W3 "innd wrote $(wc -l <"$batch" | tr -d ' ') of $narts feed lines"
        stop_innd "$bin" "$peer" "$ppid"; return 1
    fi
    cat >"$main/etc/innfeed.conf" <<EOF
backlog-directory: $main/spool/innfeed
# Hold the whole input in memory for the peer (default 10: innfeed spools
# the rest to its backlog and re-reads it only on a timer, and W3 would
# time the timer instead of the sending).
host-queue-highwater: $narts
peer perfpeer {
    ip-name:     127.0.0.1
    port-number: $pport
    # Tuned for a busy peer: four connections from the start, and up to
    # 200 articles in flight on each.
    initial-connections: 4
    max-connections:     4
    max-queue-size:      200
}
EOF
    measured "$main/innfeed.time"
    t0=$(now)
    # innfeed exits when its input ends, so keep the input open until the
    # peer has every article (or the time limit passes).
    { awk '{ print $1, $2, "perfpeer" }' "$batch"
      local waited=0
      while [ "$(spool_files "$peer")" -lt "$placements" ] && [ "$waited" -lt "$((limit * 2))" ]; do
          sleep 0.5; waited=$((waited + 1))
      done
    } | INNCONF=$main/etc/inn.conf "${measured_cmd[@]}" \
        "$bin/innfeed/innfeed" -c "$main/etc/innfeed.conf" -l "$main/innfeed.log" \
        -S "$main/innfeed.status" -p "$main/run/innfeed.pid" >"$main/innfeed.out" 2>&1
    t1=$(now)
    local got
    got=$(spool_files "$peer")
    stop_innd "$bin" "$peer" "$ppid"
    if [ "$got" != "$placements" ]; then
        failed W3 "peer has $got of $placements article files"; return 1
    fi
    out W3 wall_s "$(elapsed "$t0" "$t1")"
    rusage innfeed W3 "$main/innfeed.time"
    leakcount innfeed W3 "$main/innfeed.out"
}

# W4: 20 concurrent readers on nnrpd (standalone daemon, one child per
# connection).  /usr/bin/time on the daemon includes its reaped children.
w4() {
    local rport=$((mport + 2)) npid f=$main/w4.out
    env INNCONF=$main/etc/inn.conf ${leaks_mode:+MallocStackLogging=1} \
        /usr/bin/time -l -o "$main/nnrpd.time" \
        "$bin/nnrpd/nnrpd" -D -f -p "$rport" >"$main/nnrpd.out" 2>&1 &
    npid=$!
    local up=false
    for _ in $(seq 1 50); do
        if perl -MIO::Socket::INET -e 'exit !IO::Socket::INET->new("127.0.0.1:'"$rport"'")' 2>/dev/null; then
            up=true; break
        fi
        sleep 0.1
    done
    if ! $up; then
        failed W4 "nnrpd did not start: $(tail -2 "$main/nnrpd.out" | tr '\n' ' ')"
        kill "$npid" 2>/dev/null; return 1
    fi
    # shellcheck disable=SC2086  # the group list
    timeout "$limit" "$tools/readers" 127.0.0.1 "$rport" "$readers_n" \
        "$readers_articles" "$seed" $groups >"$f" 2>&1
    local rc=$?
    local daemon
    daemon=$(pgrep -P "$npid" || echo "$npid")
    if [ -n "$leaks_mode" ]; then
        server_leaks nnrpd-daemon W4 "$daemon" "$main/nnrpd.leaks"
        session_leaks "$rport" "$daemon"
    fi
    kill -TERM "$daemon" 2>/dev/null
    wait "$npid" 2>/dev/null
    if [ $rc -ne 0 ]; then
        failed W4 "readers: $(grep -v ' errors 0$' "$f" | head -2 | tr '\n' ' ')"; return 1
    fi
    local bad
    bad=$(awk -v p="$placements" '$1 == "session" && $4 != p' "$f" | head -1)
    if [ -n "$bad" ]; then
        failed W4 "OVER lines differ from $placements: $bad"; return 1
    fi
    out W4 wall_s "$(awk '$1 == "seconds" { print $2 }' "$f")"
    rusage nnrpd W4 "$main/nnrpd.time"
}

# session_leaks <port> <daemon-pid>: in the leak pass, one reader session
# does what a W4 session does (GROUP and OVER on every group, ARTICLE on
# some) and then waits, connected, while leaks(1) checks its nnrpd child.
session_leaks() {
    local port=$1 daemon=$2 fifo=$main/session.fifo child
    rm -f "$fifo"; mkfifo "$fifo"
    # shellcheck disable=SC2086  # the group list
    perl -MIO::Socket::INET -e '
        my ($port, $fifo, @groups) = @ARGV;
        my $s = IO::Socket::INET->new("127.0.0.1:$port") or die;
        my $l = <$s>;
        sub cmd { print $s "$_[0]\r\n"; my $r = <$s>; return $r }
        sub block { while (my $x = <$s>) { last if $x eq ".\r\n" } }
        for my $g (@groups) {
            my $r = cmd("GROUP $g");
            my (undef, undef, $lo, $hi) = split " ", $r;
            next unless $r =~ /^211/ && $hi >= $lo;
            block() if cmd("OVER $lo-$hi") =~ /^224/;
            block() if cmd("ARTICLE $lo") =~ /^220/;
        }
        print STDOUT "ready\n"; close STDOUT;
        open(my $f, "<", $fifo); <$f>;
        cmd("QUIT");' "$port" "$fifo" $groups >"$main/session.out" 2>&1 &
    local spid=$!
    for _ in $(seq 1 3000); do
        grep -q ready "$main/session.out" 2>/dev/null && break
        sleep 0.1
    done
    child=$(pgrep -P "$daemon" | head -1)
    if [ -n "$child" ]; then
        server_leaks nnrpd-session W4 "$child" "$main/session.leaks"
    else
        failed W4 "nnrpd-session: no session process to check (client: $(tail -2 "$main/session.out" | tr "\n" " "))"
    fi
    # The client may have died; never block on a FIFO nobody reads.
    if kill -0 "$spid" 2>/dev/null; then echo done >"$fifo"; fi
    wait "$spid" 2>/dev/null
}

# W5: the expire steps news.daily runs, run directly (news.daily needs an
# installed tree): expire, then expireover, with innd running.  expire.ctl
# removes the articles of half the groups at once and keeps the rest.
w5() {
    cat >"$main/etc/expire.ctl" <<'EOF'
/remember/:0
*:A:never:never:never
perf.h1.*,perf.h2.*,perf.h3.g0[1-5]:A:0:0:0
EOF
    local t0 t1 before
    before=$(spool_files "$main")
    t0=$(now)
    measured "$main/expire.time"
    INNCONF=$main/etc/inn.conf timeout "$limit" "${measured_cmd[@]}" \
        "$bin/expire/expire" >"$main/expire.out" 2>&1
    if ! ok_status $?; then
        failed W5 "expire: $(tail -2 "$main/expire.out" | tr '\n' ' ')"; return 1
    fi
    t1=$(now)
    out W5 expire_wall_s "$(elapsed "$t0" "$t1")"
    rusage expire W5 "$main/expire.time"
    leakcount expire W5 "$main/expire.out"
    t0=$(now)
    measured "$main/expireover.time"
    INNCONF=$main/etc/inn.conf timeout "$limit" "${measured_cmd[@]}" \
        "$bin/expire/expireover" >"$main/expireover.out" 2>&1
    if ! ok_status $?; then
        failed W5 "expireover: $(tail -2 "$main/expireover.out" | tr '\n' ' ')"; return 1
    fi
    t1=$(now)
    out W5 expireover_wall_s "$(elapsed "$t0" "$t1")"
    rusage expireover W5 "$main/expireover.time"
    leakcount expireover W5 "$main/expireover.out"
    local after
    after=$(spool_files "$main")
    if [ "$after" -ge "$before" ] || [ "$after" -eq 0 ]; then
        failed W5 "spool files $before before, $after after"; return 1
    fi
    out W5 spool_files_left "$after"
}

# W6: makehistory rebuilds the history from the spool, with innd stopped.
w6() {
    local t0 t1
    t0=$(now)
    measured "$main/makehistory.time"
    INNCONF=$main/etc/inn.conf timeout "$limit" "${measured_cmd[@]}" \
        "$bin/expire/makehistory" -f history.rebuilt >"$main/makehistory.out" 2>&1
    if ! ok_status $?; then
        failed W6 "makehistory: $(tail -2 "$main/makehistory.out" | tr '\n' ' ')"; return 1
    fi
    t1=$(now)
    local lines unique
    lines=$(wc -l <"$main/db/history.rebuilt" | tr -d ' ')
    unique=$(find "$main/spool/articles" -type f -exec stat -f %i {} + | sort -u | wc -l | tr -d ' ')
    if [ "$lines" != "$unique" ]; then
        failed W6 "history has $lines lines for $unique articles"; return 1
    fi
    out W6 wall_s "$(elapsed "$t0" "$t1")"
    rusage makehistory W6 "$main/makehistory.time"
    leakcount makehistory W6 "$main/makehistory.out"
}

# --- Runs ---------------------------------------------------------------------

scratch=$(mktemp -d "${TMPDIR:-/tmp}/perf.XXXXXX")
scratch=$(cd "$scratch" && pwd -P)
main=$scratch/main
cleanup() {
    pkill -f "$scratch/" 2>/dev/null
    if [ -n "${PERF_KEEP:-}" ]; then echo "kept: $scratch" >&2
    else rm -rf "$scratch"; fi
}
trap cleanup EXIT
want() { case ",$scenarios," in *,"$1",*) return 0 ;; esac; return 1; }

port_base=${PERF_PORT:-12119}
runs=0
for rep in $(seq 1 "$reps"); do
    for spec in "$@"; do
        build=${spec%%=*}
        bin=${spec#*=}
        if [ -n "${PERF_COVDIR:-}" ]; then
            mkdir -p "$PERF_COVDIR/$build"
            export LLVM_PROFILE_FILE=$PERF_COVDIR/$build/%p-%m.profraw
        fi
        # Three ports per run (innd, its peer, nnrpd), fresh each run so a
        # run never waits for the last one's sockets to close.
        mport=$((port_base + runs * 3)); runs=$((runs + 1))
        if ! setup "$bin" "$main" "$mport" main.perf.test; then
            failed setup "makedbz"; continue
        fi
        # The feed for W3: innd writes "token message-id" for each article.
        printf 'perfpeer:*:Tf,Wnm:perfpeer\n' >>"$main/etc/newsfeeds"
        if ! start_innd "$bin" "$main" "$main/innd.time"; then
            failed setup "innd did not start: $start_error"; continue
        fi
        timepid=$started_pid
        innd=$(innd_child "$timepid")
        ok=true
        for sc in W1 W2 W3 W4 W5; do
            want "$sc" || continue
            $ok || break
            lc=$(echo "$sc" | tr 'W' 'w')
            "$lc" || ok=false
        done
        server_leaks innd server "$innd" "$main/innd.leaks"
        stop_innd "$bin" "$main" "$timepid"
        rusage innd server "$main/innd.time"
        if $ok && want W6; then w6; fi
    done
done
