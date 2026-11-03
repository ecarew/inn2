#! /bin/sh
#
# Test suite for contrib/respool, with the storage that tests/data/etc
# configures (the same as storage/sm.t).  It runs in the sandbox's tests
# directory and works in a copy of tests/data.

. "$(dirname "$0")/tap.sh"
echo "1..17"

# The sandbox has tests/data; take a private copy so that the spool is ours.

for dir in "$OLDPWD/data" "$OLDPWD/../data" "$OLDPWD/tests/data"; do
    if test -r "$dir/articles/1"; then
        data=$dir
        break
    fi
done
if test -z "$data"; then
    echo "1..0 # skip no tests/data"
    exit 77
fi
cp -R "$data" data
sm=$(cd "$CONTRIB_DIR/.." && pwd)/frontends/sm
if test ! -x "$sm"; then
    echo "1..0 # skip no sm"
    exit 77
fi
cd data || exit 1
INNCONF=etc/inn.conf
export INNCONF
mkdir -p spool

prog=$CONTRIB_DIR/respool
token=$($sm -s <articles/1)
check "an article is stored" test -n "$token"
check "it is in the spool" test -r spool/example/test/1

# Refiling into the file it is already in cannot work (the Xref header names
# the article number).  The article must still be there afterwards: the old
# respool removed it first and then failed to store the new copy.
echo "$token" | "$prog" >out 2>err
check "a refile that cannot be stored fails" test $? = 1
check "and the article is still there" test -r spool/example/test/1
$sm "$token" >kept 2>/dev/null
check "with its text" diff articles/1 kept

# From now on new articles go to timehash; tradspool stays configured so that
# the old tokens still work.  (Refiling into the same tradspool file would
# collide: the Xref header names the article number.)
cat >etc/storage.conf <<'CONF'
method timehash {
    newsgroups: *
    class: 0
}
method tradspool {
    newsgroups: *
    class: 1
}
CONF

# Refile it: a new token, the old article gone, the content unchanged.
echo "$token" | "$prog" >out 2>err
check "respool succeeds" test $? = 0
check "it reports the refiling" grep -q "^refiled $token to @" out
newtoken=$(sed -n 's/^refiled .* to \(@[^ ]*\)$/\1/p' out)
check "with a new token" test -n "$newtoken" -a "$newtoken" != "$token"
check "the old article is gone" sh -c "! $sm $token >/dev/null 2>&1"
check "and its tradspool file" test ! -e spool/example/test/1
$sm "$newtoken" >refiled 2>/dev/null
check "the new article has the same text" diff articles/1 refiled
check "it is in timehash" test -n "$(find spool -path '*time-*' -type f)"
check "the reminder about history is printed" \
    grep -q 'rebuild history and overview' out

# A token that no longer exists is skipped, a malformed one is an error.
echo "$token" | "$prog" >out 2>err
check "a token that is gone is skipped" test $? = 0
echo "not-a-token" | "$prog" >out 2>err
check "a malformed token fails" test $? = 1
check "and is named" grep -q 'bad token format not-a-token' err

# Two tokens on separate lines, with a blank line between them.
t2=$($sm -s <articles/2)
t3=$($sm -s <articles/3)
printf '%s\n\n%s\n' "$t2" "$t3" | "$prog" >out 2>err
check "several tokens at once" test "$(grep -c '^refiled ' out)" = 2

exit "$failed"
