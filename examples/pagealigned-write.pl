#!/usr/bin/env perl 
use strict;
use warnings;

$|++;

use Fcntl qw(:DEFAULT O_ASYNC O_DIRECT);
use Sys::Mmap;

my $FH;
sysopen($FH, "./test.dat", O_WRONLY | O_TRUNC | O_CREAT | O_ASYNC | O_DIRECT, 0666);

my $BUFSIZE = 1048576;
my $BUFFER;

mmap($BUFFER, $BUFSIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, STDOUT) 
    or die "Couldn't mmap\n";

substr($BUFFER, 0, $BUFSIZE) = "0"x$BUFSIZE;

for (my $i = 0; $i < 20480; $i++) {
        my $written = syswrite($FH, $BUFFER, $BUFSIZE);
        die "System write error: $!\n" unless defined $written;
}

