use strict;
use warnings;

use Test::More tests => 1;

# TOOD: Does not test for data larger than SPLICE_SIZE

use Sys::Splice qw(sys_vmsplice sys_splice);
use Sys::Splice::iovec;

my $buf1 = "hello1";
my $buf2 = "hello2\n";

open my $fh, ">", "/tmp/file.file.$$";
pipe my $pipe_out, my $pipe_in or die ("Could not make pipe: $!");

my $iov = new Sys::Splice::iovec(\$buf1, \$buf2);

while(!$iov->empty()) {
    my $vmbytes = sys_vmsplice(fileno($pipe_in), $iov->struct(), $iov->size(), 0);
    if($vmbytes <= 0) {
        die "vmsplice: $!";
    }
    $iov->inc($vmbytes);

    while($vmbytes) {
        # splice the pipe to our file handle 
        my $fhbytes = sys_splice(fileno($pipe_out), 0, fileno($fh), 
            0, $vmbytes, 0);

        if($fhbytes <= 0) {
            die "splice: $!";
        }
        
        $vmbytes -= $fhbytes;
    }
}

system "cat /tmp/file.file.$$";
close $fh;

unlink "/tmp/file.file.$$";

