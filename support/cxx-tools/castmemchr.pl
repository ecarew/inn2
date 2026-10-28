#!/usr/bin/perl -i
# Usage: castmemchr.pl FILE LINE...  — wrap memchr(...) calls on the given
# lines in static_cast<char *>(...).
my ($file, @lines) = @ARGV; @ARGV = ($file); my %want = map { $_ => 1 } @lines;
while (<>) {
    s/(?<!static_cast<char \*>\()\bmemchr(\(((?:[^()]++|(?1))*)\))/static_cast<char *>(memchr$1)/g if $want{$.};
    print;
}
