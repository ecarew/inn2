#! /bin/sh
#
# Test suite for contrib/pullart.

. "$(dirname "$0")/tap.sh"
echo "1..15"

prog=$CONTRIB_DIR/pullart

# Append one article to the cyclic buffer image: a 16-byte block ending in
# "\0\0\0Path", then the rest of the article (carriage returns included),
# zero-padded to a multiple of 16 bytes.
addarticle() {
    printf '123456789\000\000\000Path' >>cycbuff
    printf "$1" >body
    size=$(wc -c <body | tr -d ' ')
    cat body >>cycbuff
    pad=$(((16 - size % 16) % 16))
    test "$pad" -gt 0 && head -c "$pad" /dev/zero >>cycbuff
}

# The first 0x38000 bytes of a buffer are the header.
head -c 229376 /dev/zero >cycbuff
addarticle ': a\r\nNewsgroups: alt.test\r\nSubject: hello\r\n\r\nbody one\r\n.\r\n'
addarticle ': b\r\nNewsgroups: comp.test\r\nSubject: other\r\n\r\nbody two\r\n.\r\n'

check "no arguments prints usage and fails" status_is 1 "$prog"
check "usage text" grep -q '^  pullart <cycbuff> <fileprefix>' out
check "a missing input file fails" status_is 1 "$prog" nosuchfile out
check "an overlong prefix is refused" status_is 1 "$prog" cycbuff \
    "$(head -c 400 /dev/zero | tr '\0' x)"

check "all articles" status_is 0 "$prog" cycbuff all
printf 'Path: a\nNewsgroups: alt.test\nSubject: hello\n\nbody one\n.\n' >want0
printf 'Path: b\nNewsgroups: comp.test\nSubject: other\n\nbody two\n.\n' >want1
check "first article, carriage returns removed" cmp -s all.000000 want0
check "second article" cmp -s all.000001 want1
check "no third file" test ! -e all.000002

check "header filter" status_is 0 "$prog" cycbuff hdr Newsgroups:
check "header filter matches both" test -e hdr.000001

check "header and string" status_is 0 "$prog" cycbuff str Newsgroups: comp
check "only the matching article" cmp -s str.000000 want1
check "and only one file" test ! -e str.000001

check "no match writes nothing" status_is 0 "$prog" cycbuff none Subject: zzz
check "no file for no match" test ! -e none.000000

exit "$failed"
