#  nnrpd Perl access hook for support/live-innd.sh (perl_access in
#  readers.conf; doc/pod/hook-perl.pod).  Returns readers.conf access
#  parameters: reading and posting in test.* only.

use strict;
use warnings;

our %attributes;

sub access {
    return (read => 'test.*', post => 'test.*');
}

1;
