#  An innd Perl filter for the filter tests: a spam filter of the kind a
#  site writes, with one rejection reason per rule, so a test can check
#  each one.  Adapted from an example the architect found online
#  (2026-11-06) to innd's interface (innd/perl.cc, doc/pod/hook-perl.pod):
#  the filter is in package main, takes no arguments and reads the
#  article's header fields from the global %hdr; innd calls no filter_init.
#
#  filter_art returns the empty string to accept the article, or the
#  reason to reject it.

use strict;
use warnings;

our %hdr;

sub filter_art {
    # 1. Excessive cross-posting: more than 5 newsgroups.
    if (defined $hdr{'Newsgroups'}) {
        my @groups = split(/,/, $hdr{'Newsgroups'});
        if (scalar(@groups) > 5) {
            return "Excessive cross-posting is limited to 5 groups max";
        }
    }

    # 2. Binaries outside binaries groups.  "part \d+" also matches an
    #    ordinary subject such as "Part 2 of my essay"; good enough for a
    #    test, too broad for a real site.
    if (defined $hdr{'Subject'} && defined $hdr{'Newsgroups'}) {
        if ($hdr{'Subject'} =~ /vset|yEnc|part \d+/i
            && $hdr{'Newsgroups'} !~ /binar/i) {
            return "Binary attachments are not allowed in text hierarchies";
        }
    }

    # 3. Known spam injection hosts in the Path header field.
    if (defined $hdr{'Path'}
        && $hdr{'Path'} =~ /roguespammer\.example|badrelay\.example/i) {
        return "Article injected via a blocked transit host";
    }

    # 4. A From header field is required.
    if (!defined $hdr{'From'} || $hdr{'From'} eq '') {
        return "Article missing From header field";
    }

    return '';
}

1;
