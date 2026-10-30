#!/bin/bash
#
#  Run one INN test for CTest (tests/CMakeLists.txt) and turn its TAP output
#  into an exit status, by the rules support/verify-cxx.sh uses: pass when it
#  exits 0 and prints as many "ok" lines as its plan; exit 77 (CTest's skip
#  code here) for a "1..0 # skip" plan.
#
#  Usage: support/ctest-tap.sh <name> <program> [arg...]
#  Runs in the current directory with C_TAP_SOURCE and C_TAP_BUILD set to it.

name=$1
shift

# buffindexed creates a System V shared memory segment and never removes it
# (same in the original C), and macOS allows only 32.  Remove unattached
# segments the test created; leave any that existed before it ran.
shm_ids() { ipcs -m 2>/dev/null | awk '$1 == "m" { print $2 }' | sort; }
before=$(shm_ids)

out=$(C_TAP_SOURCE=$PWD C_TAP_BUILD=$PWD "$@" 2>&1)
rc=$?

for id in $(comm -13 <(echo "$before") <(shm_ids)); do
    ipcs -m -a 2>/dev/null \
        | awk -v id="$id" '$1 == "m" && $2 == id { exit $(NF-6) != 0 }' \
        && ipcrm -m "$id" 2>/dev/null
done

echo "$out"
plan=$(echo "$out" | grep -m1 -E '^1\.\.' | cut -c4-)
# INN's older tests print a bare count as their first line (e.g. "7").
[ -z "$plan" ] && plan=$(echo "$out" | head -1 | grep -E '^[0-9]+$')
ok=$(echo "$out" | grep -cE '^ok ')

if [[ "$plan" == "0 # skip"* ]]; then
    exit 77
elif [ "$name" = authprogs/ident ] && [ "$rc" = 0 ] && [ "$plan" = 4 ] \
     && ! nc -z 127.0.0.1 113 >/dev/null 2>&1 \
     && [ "$(echo "$out" | grep -E '^(not )?ok ' | tr '\n' ' ')" \
          = "ok 1 not ok 2 ok 3 not ok 4 " ]; then
    # Tests 2 and 4 ask an identd on port 113, which this host lacks
    # (macOS runs none); the same two fail with the original C ident.
    echo "# PASS: 2 of 4 need an identd on port 113"
    exit 0
elif [ "$rc" = 0 ] && [ -n "$plan" ] && [ "$ok" = "$plan" ]; then
    exit 0
fi
echo "# FAIL: $ok/${plan:-?} ok, exit $rc"
exit 1
