#  nnrpd Perl authentication hook for support/live-innd.sh (perl_auth in
#  readers.conf; doc/pod/hook-perl.pod).  Accepts foo with password foopass,
#  as INN's test passwd file does, and refuses everything else.

use strict;
use warnings;

our %attributes;

sub authenticate {
    my $user = $attributes{'username'};
    my $pass = $attributes{'password'};

    return (281, '')
      if defined $user && $user eq 'foo' && defined $pass && $pass eq 'foopass';
    return (481, 'Bad user or password');
}

1;
