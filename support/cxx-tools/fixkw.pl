#!/usr/bin/perl
# Usage: fixkw.pl FILE KEYWORD FIELD-NAME IDENT-NAME
# In code only (not comments, strings or char literals): ".KEYWORD" and
# "->KEYWORD" become FIELD-NAME; any other KEYWORD identifier becomes
# IDENT-NAME.  Prints a count of each replacement.
use strict;
use warnings;

my ($file, $kw, $field, $ident) = @ARGV;
die "usage: fixkw.pl FILE KEYWORD FIELD IDENT\n" unless defined $ident;
open(my $in, '<', $file) or die "$file: $!";
my $src = do { local $/; <$in> };
close $in;

my ($nf, $ni) = (0, 0);
my $out = '';
while ($src =~ m{\G(/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[^/"']+|.)}gs) {
    my $tok = $1;
    if ($tok =~ m{^(?:/\*|//|"|')} && length($tok) > 1) {
        $out .= $tok;
        next;
    }
    $nf += ($tok =~ s/(\.|->)\s*\Q$kw\E\b/$1$field/g);
    $ni += ($tok =~ s/(?<![\w.>])\Q$kw\E\b(?!_)/$ident/g);
    $out .= $tok;
}
open(my $o, '>', $file) or die "$file: $!";
print $o $out;
close $o;
print "$file: $kw: $nf field uses -> $field, $ni identifiers -> $ident\n";
