#! /bin/sh
#
# Test suite for contrib/expirectl.  It works in the directory given with -d
# instead of /home/news.

. "$(dirname "$0")/tap.sh"
echo "1..20"

prog=$CONTRIB_DIR/expirectl

mkdir -p news/spool/news
printf '/remember/:[1.2/20]\n*:A:1:[1.0]:[6.0]\nbest.intro:A:never:never:never\n' \
    >news/expire.ctl.ctl

# Free space is below an impossibly large minimum: expiration shortens.  There
# is no expire.days yet, so it is created, starting from 2 days.
check "decrement succeeds" status_is 0 "$prog" -d news -b 100000000m -i 0
check "says it is creating expire.days" grep -q '^creating news/expire.days$' out
check "reports the decrement" grep -q '^decrement expiration to 1 days$' out
printf '/remember/:20\n*:A:1:1:6\nbest.intro:A:never:never:never\n' >want
check "expire.ctl from the template" cmp -s news/expire.ctl want
check "no temporary file is left" test ! -e news/expire.ctl.tmp
check "expire.days has the days" grep -q '^days 1$' news/expire.days

# Plenty of space and the increment is due (time is a day old): 1 day -> 2.
printf 'time 0x00000001\ndays 1\n' >news/expire.days
check "increment succeeds" status_is 0 "$prog" -d news -b 1 -i 0
check "reports the increment" grep -q '^increment expiration to 2 days$' out
printf '/remember/:20\n*:A:1:2:12\nbest.intro:A:never:never:never\n' >want
check "multipliers apply to 2 days" cmp -s news/expire.ctl want

# Just incremented, so the next run waits.
check "second run succeeds" status_is 0 "$prog" -d news -b 1 -i 0
check "waits to increment" grep -q '^will increment expiration later$' out

# -n changes nothing, -f forces a rewrite.
printf 'time 0x00000001\ndays 7\n' >news/expire.days
cp news/expire.ctl before
check "-n leaves the files alone" status_is 0 "$prog" -d news -n -b 1 -i 0
check "-n kept expire.days" grep -q '^days 7$' news/expire.days
check "-f rewrites expire.ctl" status_is 0 "$prog" -d news -f -b 1 -i 0 -v
check "-v prints the status" grep -q '^spool:' out

# Arguments: attached or separate, with suffixes; bad ones are errors.
check "-b with a suffix and a separate argument" \
    status_is 0 "$prog" -d news -b 1k -i 0
check "a missing option argument fails" status_is 1 "$prog" -d news -b
check "a bad size fails" status_is 1 "$prog" -d news -b 12x
check "an unknown option fails" status_is 1 "$prog" -z
check "a missing spool fails" status_is 1 "$prog" -d nosuchdir

exit "$failed"
