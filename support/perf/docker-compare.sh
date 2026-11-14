#!/bin/bash
#
#  Performance comparison of two revisions on Linux, in the Docker image of
#  support/docker-linux-checks.sh: builds both (RelWithDebInfo, TLS on, for
#  the container's ordinary user), builds the perf tools, generates the
#  standard feed and runs support/perf/run-compare.sh, alternating builds.
#  With -p, profiles nnrpd with callgrind instead of timing (one round per
#  build) and prints the functions whose instruction counts changed most.
#
#  Usage: support/perf/docker-compare.sh [-n reps] [-s scenarios] [-p]
#             [-o outdir] OLD NEW
#    OLD, NEW   git revisions; "." is the working tree, uncommitted changes
#               to tracked files included
#    -n reps    rounds per build (default 5; 1 with -p)
#    -s list    run-compare.sh scenarios (default W1,W4,W4T; W1 is always
#               run first, since it loads the articles the readers fetch)
#    -p         callgrind profile of nnrpd (every process, children too)
#    -o outdir  where results go (default perf-<date>-<time> under $TMPDIR)
#
#  Results: outdir/times.txt (run-compare.sh's lines) and summary.txt
#  (median per build, change, and each build's range; ranges that do not
#  overlap mean a real difference).  With -p: outdir/old.ann, new.ann
#  (callgrind_annotate per process) and profile.txt.
#
#  Docker Desktop on macOS runs everything in one Linux VM: both builds
#  share it, so the comparison is fair, but system time is VM system time
#  and the CPU is the host's (arm64 on Apple silicon).

set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
image=inn-linux-root-checks

reps= scen=W1,W4,W4T profile= out=
while getopts n:s:po: opt; do
    case $opt in
    n) reps=$OPTARG ;;
    s) scen=$OPTARG ;;
    p) profile=1 ;;
    o) out=$OPTARG ;;
    *) sed -n '10,18s/^#//p' "$0" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 2 ] || { sed -n '10,18s/^#//p' "$0" >&2; exit 2; }
[ -n "$reps" ] || reps=$([ -n "$profile" ] && echo 1 || echo 5)
case ",$scen," in *,W1,*) ;; *) scen=W1,$scen ;; esac
out=${out:-${TMPDIR:-/tmp}/perf-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"
out=$(cd "$out" && pwd)

# rev <revision>: a tree-ish for git archive ("." = the working tree).
rev() {
    if [ "$1" = . ]; then
        local c
        c=$(git stash create)
        echo "${c:-HEAD}"
    else
        git rev-parse --verify -q "$1^{commit}" \
            || { echo "unknown revision: $1" >&2; exit 2; }
    fi
}
old=$(rev "$1")
new=$(rev "$2")

docker build -q -t "$image" -f support/docker/Dockerfile.linux \
    support/docker >/dev/null || { echo "image build failed" >&2; exit 1; }

work=$(mktemp -d "${TMPDIR:-/tmp}/docker-compare.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/old" "$work/new"
git archive "$old" | tar -x -C "$work/old"
git archive "$new" | tar -x -C "$work/new"
cat >"$work/run.sh" <<EOF
reps=$reps scen=$scen profile=$profile
EOF
cat >>"$work/run.sh" <<'EOF'
set -e
cd /src
for b in old new; do
    cmake -S $b -B $b/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DINN_RUNAS_USER="$(id -un)" -DINN_RUNAS_GROUP="$(id -gn)" \
        -DINN_TLS=ON >/tmp/cmake-$b.log 2>&1 \
        || { echo "$b: configure failed"; tail -20 /tmp/cmake-$b.log; exit 1; }
    cmake --build $b/build >/tmp/build-$b.log 2>&1 \
        || { echo "$b: build failed"; tail -40 /tmp/build-$b.log; exit 1; }
done
# The tools and the harness come from the new revision, for both builds.
P=new/support/perf
mkdir tools
c++ -std=c++20 -O2 -o tools/genarticles $P/genarticles.cc
c++ -std=c++20 -O2 -o tools/feeder $P/feeder.cc
c++ -std=c++20 -O2 -o tools/readers $P/readers.cc -lssl -lcrypto
tools/genarticles 40000 0x1e57 "Wed, 04 Nov 2026 00:00:00 +0000" >feed
echo "cpus $(nproc), kernel $(uname -r)" >/out/host.txt
if [ -z "$profile" ]; then
    PERF_TOOLS=/src/tools bash $P/run-compare.sh -n "$reps" -s "$scen" feed \
        old=/src/old/build new=/src/new/build >/out/times.txt 2>/out/times.err
    exit 0
fi
# Profile: a copy of the harness per build with nnrpd under callgrind
# (forks are followed: one output file per process).
for b in old new; do
    mkdir -p cg/$b
    sed -e 's|"\$bin/nnrpd/nnrpd" "\${flags\[@\]}"|valgrind --tool=callgrind --callgrind-out-file=/src/cg/'$b'/cg.%p &|' \
        $P/run-compare.sh >rc-$b.sh
    grep -q callgrind rc-$b.sh || { echo "harness changed: cannot add callgrind"; exit 1; }
    PERF_TIMEOUT=3000 PERF_TOOLS=/src/tools bash rc-$b.sh -n "$reps" \
        -s "$scen" feed $b=/src/$b/build >>/out/times.txt 2>>/out/times.err || true
    : >/out/$b.ann
    for f in cg/$b/*; do
        callgrind_annotate --auto=no --threshold=100 "$f" >>/out/$b.ann 2>&1
    done
done
EOF
COPYFILE_DISABLE=1 tar --no-xattrs -C "$work" -cf - . \
    | docker run --rm -i -v "$out:/out" "$image" bash -c \
        'mkdir /src && tar -x -C /src 2>/dev/null && chown -R tester /src \
         && chmod 777 /out && su tester -c "bash /src/run.sh"'

echo "old $1 ($old), new $2 ($new); $(cat "$out/host.txt")" >"$out/summary.txt"
if grep -q FAILED "$out/times.txt"; then
    grep FAILED "$out/times.txt" >>"$out/summary.txt"
fi
if [ -n "$profile" ]; then
    python3 "$root/support/perf/docker-summary.py" profile \
        "$out/old.ann" "$out/new.ann" >"$out/profile.txt"
    cat "$out/summary.txt" "$out/profile.txt"
else
    python3 "$root/support/perf/docker-summary.py" times "$out/times.txt" \
        >>"$out/summary.txt"
    cat "$out/summary.txt"
fi
echo "results in $out"
