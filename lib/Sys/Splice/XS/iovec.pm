package Sys::Splice::XS::iovec;
use strict;
use warnings;

use base qw(Exporter DynaLoader);

our @EXPORT_OK = qw();
our %EXPORT_TAGS = ();

our $VERSION = '1.01';

bootstrap($VERSION);

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
    $self->{struct};
}


=item inc($int);

Increment pointer with $int bytes, moving to another buffer if necessary

=cut

sub inc {
    my ($self, $int) = @_;

    my $offset = $self->{offset} + $int; 
    my $length = length ${$self->{buffers}[$self->{index}]};

    if($offset < $length) {
        $vecs .= pack('P L', $$buf, );

    } else {

    }

    for(my $i=0; $i < int @{$self->{buffers}}; $i++) {
        $length += length $self->{buffers}[0];
    }

    my $vecs;
    foreach my $buf (@buffers) {
        $vecs .= pack('P L', $$buf, length($$buf));
    }
    
    $self->{offset} += $int;
}

=item dec($int);

Decrement pointer with $int bytes, moving to another buffer if necessary

=cut

sub dec {
    my ($self, $int) = @_;
}


=item unshift($buf);

Add new buffer to start of vector 

=cut

sub unshift {
    my ($self, $int) = @_;
}

=item shift($buf);

Remove buffer from start of vector and correct pointer if necessary 

=cut

sub shift {
    my ($self, $int) = @_;
}


=item push($buf);

Add new buffer to end of vector 

=cut

sub push {
    my ($self, $int) = @_;
}

=item pop($buf);

Remove buffer from end of vector and correct pointer if necessary 

=cut

sub pop {
    my ($self, $int) = @_;
}

sub dump_hex {
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
