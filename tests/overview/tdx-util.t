#! /bin/sh
#
# Test suite for tdx-util.
#
# Builds a small tradindexed overview from a tradspool spool and a history
# written by makehistory, then checks each tdx-util mode against it.

# The count starts at 1 and is updated each time ok is printed.  printcount
# takes "ok" or "not ok".
count=1
printcount() {
    echo "$1 $count $2"
    count=$(expr $count + 1)
}

# ok DESCRIPTION COMMAND...: print ok if the command succeeds.
ok() {
    desc=$1
    shift
    if "$@"; then
        printcount "ok" "- $desc"
    else
        printcount "not ok" "- $desc"
    fi
}

# Given two files, make sure that the first file exists and that its contents
# match the contents of the second file.
compare() {
    if [ -r "$2" ] && diff "$1" "$2"; then
        printcount "ok" "- $3"
    else
        printcount "not ok" "- $3"
    fi
}

# Find the right directory.  tdx-util is in storage/tradindexed in the Make
# build and in storage in the CMake build.
dirs='../data data tests/data'
for dir in $dirs; do
    if [ -r "$dir/articles/1" ]; then
        cd $dir
        break
    fi
done
for p in ../../storage/tradindexed/tdx-util ../../storage/tdx-util; do
    [ -x "$p" ] && tdxutil=$p
done
sm=../../frontends/sm
makehistory=../../expire/makehistory
for p in "$tdxutil" "$sm" "$makehistory"; do
    if [ ! -x "$p" ]; then
        echo "Could not find ${p:-tdx-util}" >&2
        exit 1
    fi
done

# A private copy of the test data, with every path in inn.conf absolute:
# makehistory changes to the history directory before reading the spool.
here=$(pwd)
tmp="$here/tdx-util.tmp"
rm -rf "$tmp"
mkdir -p "$tmp/db" "$tmp/etc" "$tmp/spool" "$tmp/ov" "$tmp/tmp" "$tmp/rebuild"
cp db/active db/newsgroups db/active.times db/distributions "$tmp/db/"
cp etc/storage.conf "$tmp/etc/"
sed -e "s|^pathnews:.*|pathnews: $tmp|" \
    -e "s|^patharticles:.*|patharticles: $tmp/spool|" \
    -e "s|^patharchive:.*|patharchive: $tmp/archive|" \
    -e "s|^pathoverview:.*|pathoverview: $tmp/ov|" \
    -e "s|^pathtmp:.*|pathtmp: $tmp/tmp|" etc/inn-tdx.conf >"$tmp/etc/inn.conf"
INNCONF="$tmp/etc/inn.conf"
export INNCONF
INN_TESTSUITE=true
export INN_TESTSUITE

# index_fields: the group index without the inode (field 8), which varies.
index_fields() {
    "$tdxutil" -i | cut -d' ' -f1-7
}

# Print out the number of tests.
echo 24

# -c creates groups, with the given range and flag.
"$tdxutil" -c -n example.test -a 1-10
"$tdxutil" -c -n example.config
"$tdxutil" -c -n example.moderated -f m
index_fields >"$tmp/out"
cat >"$tmp/want" <<EOF
example.test 10 1 0 0 y 0
example.config 0 1 0 0 y 0
example.moderated 0 1 0 0 m 0
EOF
compare "$tmp/out" "$tmp/want" "-c creates groups with range and flag"

# Store articles 1-4 (three in example.test, two in example.config, one
# crossposted) and write their history.
for a in 1 2 3 4; do
    "$sm" -s <articles/$a
done >"$tmp/tokens"
"$makehistory"
ok "makehistory wrote four history lines" \
    [ "$(wc -l <"$tmp/db/history" | tr -d ' ')" = 4 ]

# -R rebuilds a group's overview from a directory of articles.
"$tdxutil" -R "$tmp/spool/example/test" -n example.test 2>"$tmp/err"
ok "-R exits 0" [ $? = 0 ]
ok "-R prints no warnings" [ ! -s "$tmp/err" ]
"$tdxutil" -i -n example.test | cut -d' ' -f1-7 >"$tmp/out"
echo 'example.test 3 1 1 3 y 0' >"$tmp/want"
compare "$tmp/out" "$tmp/want" "-i -n shows the rebuilt group's range"

# -g dumps the group's data index: number, offset, length, arrival,
# expires and token for each article.
"$tdxutil" -g -n example.test | cut -d' ' -f1,2,3,5 >"$tmp/out"
printf '1 0 133 0\n2 133 168 0\n3 301 247 1084956201\n' >"$tmp/want"
compare "$tmp/out" "$tmp/want" "-g shows offsets, lengths and expires"
"$tdxutil" -g -n example.test | cut -d' ' -f6 | sort >"$tmp/out"
grep '0000000[123]00000000@$' "$tmp/tokens" | grep '@0500000000040' \
    | sort >"$tmp/want"
compare "$tmp/out" "$tmp/want" "-g shows the stored tokens"

# -o dumps overview data; -O dumps it as overchan input (token, arrival,
# expires, then the data without the article number).
"$tdxutil" -o -n example.test | cut -f1,5 >"$tmp/out"
cat >"$tmp/want" <<EOF
1	<example-1@example.com>
2	<example-3@example.com>
3	<example-4@example.com>
EOF
compare "$tmp/out" "$tmp/want" "-o shows article numbers and message IDs"
"$tdxutil" -O -n example.test -a 2-3 | cut -d' ' -f1 >"$tmp/out"
grep '0000000[23]00000000@$' "$tmp/tokens" | grep '@0500000000040' \
    >"$tmp/want"
compare "$tmp/out" "$tmp/want" "-O -a 2-3 shows tokens in overchan format"
"$tdxutil" -O -n example.test -a 3 | cut -d' ' -f3 >"$tmp/out"
echo 1084956201 >"$tmp/want"
compare "$tmp/out" "$tmp/want" "-O shows the Expires time"

# Article ranges: open at either end, a single number, and "-".
ok "-a 2- selects 2 and 3" \
    [ "$("$tdxutil" -o -n example.test -a 2- | cut -f1 | tr '\n' ' ')" = "2 3 " ]
ok "-a -2 selects 1 and 2" \
    [ "$("$tdxutil" -o -n example.test -a -2 | cut -f1 | tr '\n' ' ')" = "1 2 " ]
ok "-a - selects everything" \
    [ "$("$tdxutil" -o -n example.test -a - | wc -l | tr -d ' ')" = 3 ]

# -R with an article that is not in the history (article 5) stores the rest
# and warns about it; file names that are not numbers are ignored.
cp articles/1 "$tmp/rebuild/1"
cp articles/5 "$tmp/rebuild/5"
cp articles/2 "$tmp/rebuild/12"
cp articles/3 "$tmp/rebuild/notes"
"$tdxutil" -R "$tmp/rebuild" -n example.config 2>"$tmp/err"
ok "-R warns about an article missing from history" \
    grep -q 'cannot find article <example-5@example.com> in history' "$tmp/err"
ok "-R stores the others" \
    [ "$("$tdxutil" -o -n example.config | cut -f1 | tr '\n' ' ')" = "1 12 " ]
"$tdxutil" -R "$tmp/none" -n example.config 2>"$tmp/err"
status=$?
ok "-R fails on a missing directory" \
    sh -c "[ $status != 0 ] && grep -q 'cannot open directory' '$tmp/err'"

# -A audits the index against active; -F also fixes what it finds.
"$tdxutil" -A 2>"$tmp/err"
ok "-A reports groups missing from overview" \
    grep -q 'example.weird missing from overview' "$tmp/err"
"$tdxutil" -F 2>/dev/null
"$tdxutil" -A 2>"$tmp/err"
ok "-F fixes them" [ ! -s "$tmp/err" ]

# Errors.
"$tdxutil" -i -g 2>"$tmp/err"
ok "two modes are refused" grep -q 'only one mode option allowed' "$tmp/err"
"$tdxutil" -o 2>"$tmp/err"
ok "-o needs a group" grep -q 'group must be specified for -o' "$tmp/err"
"$tdxutil" -o -n example.test -a 2-x 2>"$tmp/err"
ok "a bad range is refused" grep -q 'invalid article number or range' "$tmp/err"
"$tdxutil" -Z 2>"$tmp/err"
ok "an unknown option is refused" grep -q 'invalid option Z' "$tmp/err"
"$tdxutil" -o -n example.nosuch 2>"$tmp/err"
ok "an unknown group is reported" \
    grep -q 'cannot find group example.nosuch' "$tmp/err"

# With no mode, tdx-util says a group is needed rather than that a mode is:
# strchr("cgoOR", mode) matches the string's terminating NUL when mode is
# '\0'.  Upstream behavior, kept; this check records it.
"$tdxutil" 2>"$tmp/err"
ok "no mode is refused (with the group message)" \
    grep -q 'group must be specified for -' "$tmp/err"

# All done.  Clean up.
rm -rf "$tmp"
