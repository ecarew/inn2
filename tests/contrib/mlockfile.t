#! /bin/sh
#
# Test suite for contrib/mlockfile.  The program normally runs forever; -1
# makes one pass and exits.

. "$(dirname "$0")/tap.sh"
echo "1..14"

prog=$CONTRIB_DIR/mlockfile

check "no arguments prints usage" status_is 64 "$prog"
check "usage text" grep -q '^usage: ' err
check "an unknown option fails" status_is 64 "$prog" -z file
check "a bad interval fails" status_is 64 "$prog" -i abc -1 file
check "a zero interval fails" status_is 64 "$prog" -i 0 -1 file
check "a negative offset fails" status_is 64 "$prog" -1 'file@-1'
check "a bad offset fails" status_is 64 "$prog" -1 'file@x'
check "a bad length fails" status_is 64 "$prog" -1 'file@0:y'
check "a bad separator fails" status_is 64 "$prog" -1 'file@0;1'
check "a missing file is reported" status_is 71 "$prog" -1 nosuchfile
check "with its name" grep -q "can't open .nosuchfile'" err

# A page of data.  Locking needs a memory limit that allows it: when this
# system refuses, say so instead of failing.
head -c 8192 /dev/zero >data
"$prog" -1 data >out 2>err </dev/null
rc=$?
if test $rc = 0; then
    check "locks a whole file" true
    check "locks part of a file" status_is 0 "$prog" -1 data@0:4096
elif grep -q "can't mlock" err; then
    count=$((count + 1))
    echo "ok $count # skip mlock is not permitted here"
    count=$((count + 1))
    echo "ok $count # skip mlock is not permitted here"
else
    check "locks a whole file" false
    check "locks part of a file" false
fi
check "an unaligned offset is reported" status_is 71 "$prog" -1 data@1

exit "$failed"
