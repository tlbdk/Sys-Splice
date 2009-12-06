use strict;
use warnings;

use Test::More tests => 1;

# TOOD: Does not test for data larger than SPLICE_SIZE

use Sys::Splice qw(vmsplice_tofh);

# Create new splice object
my $splice = new Sys::Splice;
 
# Open file for writing
open my $fh, ">", "/tmp/test.file.$$";

my $buf1 = "Hello\n";
my $buf2 = "World\n";

# Write both buffers to file handle
vmsplice_tofh($fh, $buf1, $buf2);

system "cat /tmp/test.file.$$";

close $fh;
unlink "/tmp/test.file.$$";
