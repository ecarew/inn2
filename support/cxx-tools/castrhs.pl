#!/usr/bin/perl
# Usage: castrhs.pl FILE LINE TYPE [LINE TYPE...] — on each LINE, rewrite the
# single-line "= EXPR;" as "= static_cast<TYPE>(EXPR);".
use strict; use warnings;
my ($file, %want) = @ARGV;
open(my $in, '<', $file) or die; my @l = <$in>; close $in;
for my $n (sort { $a <=> $b } keys %want) {
    my $t = $want{$n};
    $l[$n-1] =~ s/(?<![=!<>])=\s*(?!=)(.+?);(\s*)$/= static_cast<$t>($1);$2/
        or die "line $n: no single-line assignment found: $l[$n-1]";
    print "$n: $l[$n-1]";
}
open(my $o, '>', $file) or die; print $o @l; close $o;
