package Sys::Splice;
use strict;
use warnings;

# ibauthen-smb-perl-0.91, iovec and writev example

# http://www.google.com/codesearch?hl=en&q=show:ZNnmf96IH5w:LY_QvFQjmpU:Z4ek9jIGAJ8&sa=N&ct=rd&cs_p=http://cheeseshop.python.org/packages/source/s/splicetee/splicetee-1.0.tar.gz&cs_f=splicetee-1.0/test_splicetee.py

# Example on using splice:
# http://svn.linuxvirtualserver.org/repos/tcpsp/

# Guide to programming XS:
# http://world.std.com/~swmcd/steven/perl/pm/xs/intro/

# More Examples on using splice(Good)
# git://git.kernel.dk/data/git/splice

# TODO: Use in IO::EventMux
# S_IFREG S_IFDIR S_IFLNK S_IFBLK S_IFCHR S_IFIFO S_IFSOCK S_IFWHT S_ENFMT

use base "Exporter";

our @EXPORT_OK = qw(vmsplice_tofh sys_splice sys_vmsplice);
our %EXPORT_TAGS = ();

our $VERSION = '1.01';

=head1 NAME

Sys::Splice - Give access to Linux the splice/tee/vmsplice system calls from Perl

=head1 DESCRIPTION

This is simple module that wraps the splice/tee/vmsplice system call on Linux. 

=head1 SYNOPSIS
  
  use strict;
  use warnings;

  use Sys::Splice;
 
  # Create new splice object
  my $splice = new Sys::Splice;
   
  # Open file for writing
  open my $fh, ">", "test.file";

  my $buf1 = "Hello\n";
  my $buf2 = "World\n";

  # Write both buffers to file handle
  $splice->vmsplice_buffers($fh, $buf1, $buf2);

  close $fh;

  sys_vmsplice();

=head1 METHODS

=over

=cut

use IO::Socket::INET;
use IO::Select;
use Socket;
use Data::Dumper;
use Fcntl qw(:mode);

use POSIX qw(uname);
use Config;


our $SYS_splice;
our $SYS_tee;
our $SYS_vmsplice;

our $SYS_SPLICE_F_MOVE     = 0x01;
our $SYS_SPLICE_F_NONBLOCK = 0x02;
our $SYS_SPLICE_F_MORE     = 0x04;
our $SYS_SPLICE_F_GIFT     = 0x08;
our $SYS_SPLICE_F_UNMAP    = 0x10;

our $SYS_SPLICE_SIZE       = (64*1024);

# Syscall list: linux-2.6/arch/x86/kernel/syscall_table_32.S

if($^O eq 'linux') {
    my ($sysname, $nodename, $release, $version, $machine) = 
        POSIX::uname();
    
    # if we're running on an x86_64 kernel, but a 32-bit process,
    # we need to use the i386 syscall numbers.
    if ($machine eq "x86_64" && $Config{ptrsize} == 4) {
        $machine = "i386";
    }

    if ($machine =~ m/^i[3456]86$/) {
        $SYS_splice = 313;
        $SYS_tee = 315;
        $SYS_vmsplice = 316;
    
    } elsif ($machine eq "x86_64") {
        $SYS_splice = 275;
        $SYS_tee = 276;
        $SYS_vmsplice = 278;
    
    } elsif ($machine eq "ppc") {
        $SYS_splice = 283;
        $SYS_tee = 284;
        $SYS_vmsplice = 285;

    } elsif ($machine eq "ia64") {
        $SYS_splice = 1297;
        $SYS_tee = 1301;
        $SYS_vmsplice = 1302;
    
    } else {
        die "Unsupported arch";
    }
}

=item new()

Creates a new Sys::Prctl object.

=cut

sub new {
    my ($class, %opts) = @_;

    my %self = (
    
    );
    
    return bless \%self, (ref $class || $class);
}


=item sys_splice($fd_in, $off_in, $fd_out, $off_out, $len, $flags)

Direct wrapper for splice system call

=cut

sub sys_splice {
    my ($fd_in, $off_in, $fd_out, $off_out, $len, $flags) = @_;
    syscall($SYS_splice, $fd_in, $off_in, $fd_out, $off_out, $len, $flags);
}


=item sys_tee($fd_in, $fd_out, $len, $flags)

Direct wrapper for tee system call

=cut

sub sys_tee {
    my ($fd_in, $fd_out, $len, $flags) = @_;
    syscall($SYS_tee, $fd_in, $fd_out, $len, $flags);
}

=item sys_vmsplice($fd, $iov, $nr_segs, $flags)

Direct wrapper for vmsplice system call

=cut

sub sys_vmsplice {
    my ($fd, $iov, $nr_segs, $flags) = @_;
    syscall($SYS_vmsplice, $fd, $iov, $nr_segs, $flags);
}


=item vmsplice_tofh($fh, @buffers)

Copy a list of buffers to a file handle 

=cut

sub vmsplice_tofh {
    my ($fh, @bufs) = @_;
  
    pipe my $pipe_out, my $pipe_in or die ("Could not make pipe: $!");
    
    my $vecs;
    foreach my $buf (@bufs) {
       $vecs .= pack('P L', $buf, length($buf));
    }
    
    dump_hex($vecs);
   
    my $nr_segs = int @bufs; 
    my $written = sys_vmsplice(fileno($pipe_in), $vecs, $nr_segs, 0);
    print "$written\n";
    if($written < 0) {
        die "Could vmsplice: $!";
    }


    use IO::Poll qw(POLLOUT);
    my $poll = new IO::Poll;
    $poll->mask($fh => POLLOUT);
    # Wait until there is room in output
    $poll->poll();

    # splice the pipe to our file handle 
    $written = sys_splice(fileno($pipe_out), 0, fileno($fh), 
        0, $written, $SYS_SPLICE_F_MOVE);
}


#open my $fh_out, ">", "README.write" or die("Could not open README.write: $!");
#vmsplice_buffers($pipe_in, "A0A1A2A3A4A5\n", "A6A7A8A9B0B1\n");

=item splice_cp($fh_in, $fh_out)

Copy the content of one file to another by using the splice syscall

=cut

sub splice_cp {
    my ($fh_in, $fh_out) = @_;
    my @fh_stat = stat $fh_in;
    
    pipe my $pipe_out, my $pipe_in or die ("Could not make pipe: $!");

    while($fh_stat[7]) {
        my $ret = sys_splice(fileno($fh_in), 0, fileno($pipe_in), 0, 
            min($SYS_SPLICE_SIZE, $fh_stat[7]), $SYS_SPLICE_F_MOVE);
       
        if($ret < 0) {
            die "$!";
    
        } elsif(!$ret) {
            last;
        }
    
        while($ret) {
            my $written = sys_splice(fileno($pipe_out), 0, fileno($fh_out), 
                0, $ret, $SYS_SPLICE_F_MOVE);
            if($written < 0) {
                die "$!";
            }
    		$ret -= $written;
        }
    }
}

sub min {
    $_[0] < $_[1] ? $_[0] : $_[1];
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
