#! /bin/sh
#
# Test suite for contrib/reset-cnfs.

. "$(dirname "$0")/tap.sh"
echo "1..9"

prog=$CONTRIB_DIR/reset-cnfs

# 100000 bytes of "A": the program zeroes (100000 / 4096 / 512 + 50) * 512
# = 25600 bytes from the start and leaves the rest alone.
head -c 100000 /dev/zero | tr '\0' A >buf
check "reset succeeds" status_is 0 "$prog" buf
check "reports the size and block count" grep -q '^File buf: 100000 50$' out
check "size is unchanged" test "$(wc -c <buf | tr -d ' ')" = 100000
head -c 25600 buf | tr -d '\0' >zeroed
check "the first 25600 bytes are zero" test ! -s zeroed
tail -c +25601 buf | tr -d A >rest
check "the rest is untouched" test ! -s rest

check "a missing file fails" status_is 1 "$prog" nosuchfile
check "and says why" grep -q 'Could not open file nosuchfile' err

# An earlier failure must not be forgotten when a later file succeeds.
head -c 100000 /dev/zero | tr '\0' A >buf2
check "failure on the first of two files is kept" \
    status_is 1 "$prog" nosuchfile buf2
head -c 25600 buf2 | tr -d '\0' >zeroed2
check "the second file was still reset" test ! -s zeroed2

exit "$failed"
