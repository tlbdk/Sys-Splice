use strict;
use warnings;

use Test::More tests => 8;

use Sys::Splice::iovec qw(dumphex);

my $buf1 = "hello11";
my $buf2 = "hello22";
my $buf3 = "hello33";

my $iov = new Sys::Splice::iovec(\$buf1, \$buf2, \$buf3);

is($iov->struct(), generate_vecs(\$buf1, \$buf2, \$buf3), "Initial structure");

is($iov->size(), 3, "Number of vecs is 3 in object");

# points to start of buf2
$iov->inc(7);

is($iov->size(), 2, "Number of vecs is 2 in object");

is($iov->struct(), generate_vecs(\$buf2, \$buf3), "Incremented to next secment");

# points to middle of $buf3
$iov->inc(7+3);
my $vecs = pack("L!L", memory_address(\$buf3)+3, length($buf3)-3);
is($iov->struct(), $vecs, "Incremented to middle of next secment");

# points to end
$iov->inc(4);
is($iov->struct(), '', "Incremented to end");
is($iov->empty(), 1, "Incremented to end");
is($iov->size(), 0, "Number of vecs is 0 in object");

#dumphex($vecs);
#print "---------------------------\n";
#dumphex($iov->struct());
  
# points to middle in buf1
#$iov->dec(5);

sub generate_vecs {
    my $str;
    foreach my $buf (@_) {
        $str .= pack('P L', $$buf, length($$buf));
    }
    return $str;
}

sub memory_address {
    my($buf) = @_;
    #print "mem1: ${$buf}\n";
    unpack("L!", pack('P', ${$buf}));
}

sub memory_address2 {
    #print "mem2: $_[0]\n";
    unpack("L!", pack('P', $_[0]));
}

#my $a1 = unpack("L!", pack('P', $buf1));
#print "mem0: $buf1\n";
#my $a2 = memory_address(\$buf1);
#my $a3 = memory_address2($buf1);
#my $a4;
#foreach my $var ($buf1) {
#    $a4 = unpack("L!", pack('P', $var));
#}
#my $var = $buf1;
#my $a5 = unpack("L!", pack('P', $var));
#
#print "$a1, $a2, $a3, $a4, $a5\n";
#exit;
