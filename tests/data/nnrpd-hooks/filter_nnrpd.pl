#  nnrpd Perl posting filter for support/live-innd.sh, installed as
#  filter_nnrpd.pl in pathfilter (doc/pod/hook-perl.pod).  The rule of the
#  shipped sample's commented-out example: refuse "make money" subjects.

use strict;
use warnings;

our %hdr;

sub filter_post {
    my $subject = defined $hdr{'Subject'} ? $hdr{'Subject'} : '';

    return 'Spam is not acceptable here' if $subject =~ /make.*money/i;
    return '';
}

1;
