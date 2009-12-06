package Sys::Splice::iovec;
use strict;
use warnings;
use Carp;

use base "Exporter";

our @EXPORT_OK = qw(dumphex);
our %EXPORT_TAGS = ();

our $VERSION = '1.01';

=head1 NAME

Sys::Splice::iovec - Simple interface to working with C iovec structs as used by vmsplice, readv, writev

=head1 DESCRIPTION

This is simple module that wraps the a C iovec struct

=head1 SYNOPSIS
  
  use strict;
  use warnings;

  use Sys::Splice::iovec;

  my $buf1 = "Hello1";
  my $buf2 = "Hello2";

  # points to start of buf1
  $iov = new Sys::Splice::iovec($buf1, $buf2);
  
  # points to start of buf2
  $iov->inc(6);
  
  # points to e in buf1
  $iov->dec(5);


=head1 METHODS

=over

=cut

=item new()

Creates a new Sys::Splice::iovec object.

=cut

sub new {
    my ($class, @buffers) = @_;

    my %self = (
        buffers => [@buffers],
        index  => 0,
        offset => 0,
        struct => '',
        empty  => 0,
    );

    my $vecs;
    foreach my $buf (@buffers) {
        $vecs .= pack('P L', $$buf, length($$buf));
    }

    $self{struct} = $vecs;

    return bless \%self, (ref $class || $class);
}

=item struct($int);

Return C data structure

=cut

sub struct {
    my ($self) = @_;
    # Return iov's still within index
    substr($self->{struct}, $self->{index} * 8);
}


=item empty

Returns true if no more is left in the buffer

=cut

sub empty {
    my ($self) = @_;
    return $self->{empty};
}


=item size

Returns the number of vecs in object

=cut

sub size {
    my ($self) = @_;
    return @{$self->{buffers}} - $self->{index};
}

=item inc($int);

Increment pointer with $int bytes, moving to another buffer if necessary

=cut

sub inc {
    my ($self, $n) = @_;

    my $offset = $self->{offset} + $n;
    my $index = $self->{index};
    my $buf = $self->{buffers}[$self->{index}]; 
    my $length = length $$buf;

    croak "Cannot increment past end of buffer" if !defined $buf;

    while($length <= $offset and $index < @{$self->{buffers}}) {
        $offset = $offset-$length;
        $index += 1; 
        $buf = $self->{buffers}[$index];
        
        if(!defined $buf) {
            $self->{empty} = 1;
            $self->{index} += 1;
            return;
        }

        $length = length $$buf;
    }

    print "lenght,offset: $length, $offset\n";
    
    # Increment pointer and set new length
    substr($self->{struct}, $index * 8, 8, pack("L!L", 
        unpack("L!", pack('P', $$buf)) # Get memory address of $$buf
        + $offset, $length-$offset));

    $self->{offset} = $offset;
    $self->{index} = $index;
}

=item dec($int);

FIXME: Not implemented

Decrement pointer with $int bytes, moving to another buffer if necessary

=cut

sub dec {
    my ($self, $int) = @_;
}


=item unshift($buf);

FIXME: Not implemented

Add new buffer to start of vector 

=cut

sub unshift {
    my ($self, $int) = @_;
}

=item shift($buf);

FIXME: Not implemented

Remove buffer from start of vector and correct pointer if necessary 

=cut

sub shift {
    my ($self, $int) = @_;
}


=item push($buf);

FIXME: Not implemented

Add new buffer to end of vector 

=cut

sub push {
    my ($self, $int) = @_;
}

=item pop($buf);

FIXME: Not implemented

Remove buffer from end of vector and correct pointer if necessary 

=cut

sub pop {
    my ($self, $int) = @_;
}

sub dumphex {
    my ($str) = @_;
    for(my $i=0; $i < length($str); $i++) {
        my $char = substr($str, $i, 1);
        print(sprintf("%02d: x0%02x(%03d)", 
            $i, ord($char), ord($char))."\n");
    }
}

=head1 NOTES

Currently only 32bit Linux has been tested. So test reports and patches are 
wellcome. 

=head1 AUTHOR

Troels Liebe Bentsen <tlb@rapanden.dk> 

=head1 COPYRIGHT

Copyright(C) 2005-2007 Troels Liebe Bentsen

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself.

=cut

1;
