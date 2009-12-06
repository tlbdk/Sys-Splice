#!/usr/bin/perl
# $Id: CCE.pm,v 1.3 2005/08/23 18:25:01 dodell Exp $
# Copyright 2001 Sun Microsystems, Inc.  All rights reserved.
#
# a client library for communication between perl and cce

$| = 1; # always always always

package CCE;

use UnixAuth;
use vars qw/ $DEBUG /;
$DEBUG = 2;

sub new
{
  my $proto = shift;
  my $class = ref($proto) || $proto;
  my $self = {};
  bless ($self, $class);
  $self->init(@_);
  return $self;
}

sub init
{
  my $self = shift;
  my %opts = @_;

  $self->{rdsock} = STDIN;
  $self->{wrsock} = STDOUT;
  $self->{suspendedmsg} = undef;
  $self->{debug} = 0;
  $self->{rollbackflag} = 0;

  $self->{event} = "";
  $self->{event_oid} = "";
  $self->{event_namespace} = "";
  $self->{opt_namespace} = $opts{"Namespace"};
  $self->{event_property} = "";
  $self->{event_class} = "";

  $self->{event_object} = {};
  $self->{event_old} = {};
  $self->{event_new} = {};

  $self->{event_create} = 0;
  $self->{event_destroy} = 0;

  $self->{msgref} = undef;

  $self->{domain} = $opts{'Domain'};

  $self->flushmsgs();
}

sub event_oid { my $self = shift; return $self->{event_oid}; }
sub event_namespace { my $self = shift; return $self->{event_namespace}; }
sub event_property { my $self = shift; return $self->{event_property}; }
sub event_object { my $self = shift; return $self->{event_object}; }
sub event_old { my $self = shift; return $self->{event_old}; }
sub event_new { my $self = shift; return $self->{event_new}; }
sub event_is_create { my $self = shift; return $self->{event_create}; }
sub event_is_destroy { my $self= shift; return $self->{event_destroy}; }
sub event_is_modify {
  my $self = shift;
  return (!$self->{event_destroy} && !$self->{event_create});
}

sub flushmsgs()
{
  my $self = shift;
  $self->{success} = 0;
  $self->{perm} = 1;
  $self->{object} = {};
  $self->{old} = {};
  $self->{new} = {};
  $self->{baddata} = {};
  $self->{info} = [];
  $self->{oidlist} = [];
  $self->{namelist} = [];
  $self->{createflag} = 0;
  $self->{destroyflag} = 0;
  $self->{sessionid} = "";
  $self->{classlist} = [];
}

sub read_header
{
  my $self = shift;
  $self->_recv();
  # Don't have to worry if namespace is undef, the get function
  # will just not use it.
  if ($self->{event_oid}) {
    # we don't get the event_namespace because of backwards compatibility
    $self->get($self->{event_oid},$self->{opt_namespace});
    $self->{event_object} = $self->{object};
    $self->{event_old} = $self->{old};
    $self->{event_new} = $self->{new};
    $self->{event_create} = $self->{createflag};
    $self->{event_destroy} = $self->{destroyflag};
  } else {
    $self->{event_object} = undef;
    $self->{event_old} = undef;
    $self->{event_new} = undef;
    $self->{event_create} = 0;
    $self->{event_destroy} = 0;
  }
}

sub connectuds
{
  my $self = shift;
  my $filename = shift || '(prefix)/raqdevil/cced.socket';
  require IO::Socket::UNIX;
  import IO::Socket::UNIX;
  my $sock = new IO::Socket::UNIX(
    'Type' => 1, # SOCK_STREAM,
    'Peer' => $filename,
  );
  if (!$sock) {
    die "Could not connect to $filename: $!\n";
  }
  $self->{rdsock} = $sock;
  $self->{wrsock} = $sock;
  $self->{suspendedmsg} = undef;
  $self->{rollbackflag} = 0;

  UnixAuth::authenticate(fileno($sock));

  # get the header
  $self->read_header();
}

# if we're suspended, return the reason (may be blank), else return undef
sub suspended
{
  my $self = shift;

  return $self->{suspendedmsg};
}

# used by handlers - BAD NEWS if it fails
sub connectfd
{
  my $self = shift;

  $self->{rdsock} = \*STDIN;
  $self->{wrsock} = \*STDOUT;
  $self->{suspendedmsg} = undef;
  $self->{rollbackflag} = 0;

  # get the header
  $self->read_header();
}

# used by handlers to determine if they are in rollback mode or not
sub is_rollback
{
  my $self = shift;

  return $self->{rollbackflag};
}

sub _disconnect
{
  my $self = shift;
  $self->{rdsock} = undef;
  $self->{wrsock} = undef;
  $self->{suspendedmsg} = undef;
  $self->{rollbackflag} = 0;
}

sub _recv
{
  my $self = shift;
  my $rdsock = $self->{rdsock} || \*STDIN;
  my @msgs = ();
  while (defined($_ = <$rdsock>)) {
    if ($DEBUG) { print STDERR "< ", $_; }
    push (@msgs, $_);
    last if (m/^[24]/);
  }
  $self->{msgref} = \@msgs;
  $self->_parse_response();
  return @msgs;
}

# note: multiple regexps does NOT work..has to be done in one pass
sub _escape
{
        my $self = shift;
        my $text = shift;
        my (@pattern, $i, $out);

        $out = "";
        if (ref($text) eq "ARRAY") {
                $text = CCE->array_to_scalar(@$text);
        }
        @pattern = split(//,$text);

        if (!defined($text)) { $text = ""; }
        if ($text =~ m/^[a-zA-Z0-9_]+$/) {
                return $text;
        }

        for ($i = 0; $i <= $#pattern; $i++) {
                # handle an escapable char
                CASE: {
                        $pattern[$i] eq "\a" && do {
                                $out .= "\\a";
                                last CASE;
                        };
                        $pattern[$i] eq "\b" && do {
                                $out .= "\\b";
                                last CASE;
                        };
                        $pattern[$i] eq "\f" && do {
                                $out .= "\\f";
                                last CASE;
                        };
                        $pattern[$i] eq "\n" && do {
                                $out .= "\\n";
                                last CASE;
                        };
                        $pattern[$i] eq "\r" && do {
                                $out .= "\\r";
                                last CASE;
                        };
                        $pattern[$i] eq "\t" && do {
                                $out .= "\\t";
                                last CASE;
                        };
                        #$pattern[$i] eq "\v" && do {
                        #       $out .= "\\v";
                        #       last CASE;
                        #};
                        $pattern[$i] eq "\"" && do {
                                $out .= "\\\"";
                                last CASE;
                        };
                        $pattern[$i] eq "\\" && do {
                                $out .= "\\\\";
                                last CASE;
                        };
                        $out .= $pattern[$i];
                }
        }
        $out = "\"$out\"";
        return $out;
}

# note: multiple regexps does NOT work..has to be done in one pass
sub unescape
{
        my $self = shift;
        my $text = shift;
        my (@pattern, $i, $out);
        if (!defined($text)) { $text = $self; }
        if ($text =~ m/^\"(.*)\"$/) {
                $text = $1;
        }

        @pattern = split(//,$text);

        $out = "";
        for ($i = 0; $i <= $#pattern; $i++) {
                if ($pattern[$i] eq "\\") {
                  # handle an escaped char
                        CASE: {
                                $pattern[$i+1] eq "a" && do {
                                        $out .= "\a";
                                        last CASE;
                                };
                                $pattern[$i+1] eq "b" && do {
                                        $out .= "\b";
                                        last CASE;
                                };
                                $pattern[$i+1] eq "f" && do {
                                        $out .= "\f";
                                        last CASE;
                                };
                                $pattern[$i+1] eq "n" && do {
                                        $out .= "\n";
                                        last CASE;
                                };
                                $pattern[$i+1] eq "r" && do {
                                        $out .= "\r";
                                        last CASE;
                                };
                                $pattern[$i+1] eq "t" && do {
                                        $out .= "\t";
                                        last CASE;
                                };
                                #$pattern[$i+1] eq "v" && do {
                                #       $out .= "\v";
                                #       last CASE;
                                #};
                                $pattern[$i+1] eq "\"" && do {
                                        $out .= "\"";
                                        last CASE;
                                };
                                $pattern[$i+1] eq "\\" && do {
                                        $out .= "\\";
                                        last CASE;
                                };
                                # handles out-of-range octals
                                $pattern[$i+1] =~ /[0-9]/ && do {
                                        # convert octal to decimal
                                        my $decimal = $pattern[$i+1] * 64
                                                + $pattern[$i+2] * 8
                                                + $pattern[$i+3];
                                        $out .= pack("C", $decimal);
                                        $i += 2;
                                        last CASE;
                                };
                                $out .= $pattern[$i+1];
                        }
                        $i++;
                } else {
                        $out .= $pattern[$i];
                }
        }
        return $out;
}

sub _send
{
  my $self = shift;
  my $wrsock = $self->{wrsock} || \*STDOUT;

  my @encoded = ();
  foreach $_ (@_) {
    if (ref($_) eq "HASH") {
      my ($key,$val);
      while (($key,$val) = each %$_) {
        push (@encoded,
          $self->_escape($key) . '=' . $self->_escape($val) );
      }
    } elsif ( /^\d+\.\w+$/ ) {
      # Check if it looks like a namespace. (12.Namespace)
      push (@encoded, $_);
    } else {
      push (@encoded, $self->_escape($_));
    }
  }

  my $str = join(" ",@encoded)."\n";
  print $wrsock $str;
  if ($DEBUG) { print STDERR "> ", $str; }
}

sub _testsuccess
{
  my $self = shift;
  $_ = $_[$#_];
  return (m/^2/);
}

sub _parse_response
{
  my $self = shift;

  $self->flushmsgs();

  foreach $_ (@{$self->{msgref}})
  {
    if (m/^100 CSCP\/(\S+)/) { $self->{version} = $1; next; }
    if (m/^101/) {
      $self->{event} = "unknown";
      $self->{event_oid} = 0;
      $self->{event_namespace} = "";
      $self->{event_property} = "";
      # FIXME: this needs to handle multiple header EVENTs
      if (m/EVENT\s+(\d+)\s*\.\s*(\w*)\s*\.\s*(\w*)/) {
        $self->{event_oid} = $1;
        $self->{event_namespace} = $2;
        $self->{event_property} = $3;
      } elsif (m/EVENT\s+(\d+)\s*\.(\w*)/) {
        $self->{event_oid} = $1;
        $self->{event_property} = $2;
      }
      next;
    }
    if (m/^102 \S+ (.*?)\s*=\s*(.*)/) {
      my ($key, $val) = (unescape($1),unescape($2));
      $self->{old}->{$key} = $val;
      next;
    }
    if (m/^103 \S+ (.*?)\s*=\s*(.*)/) {
      my ($key, $val) = (unescape($1),unescape($2));
      $self->{new}->{$key} = $val;
      next;
    }
    if (m/^104 OBJECT (\d+)/) { push (@{$self->{oidlist}}, $1); next; }
    if (m/^105 NAMESPACE (\S+)/) { push (@{$self->{namelist}}, $1); next; }
    if (m/^106 INFO (.*)/) { push(@{$self->{info}}, $1); next; }
    if (m/^107 CREATED/) { $self->{createflag} = 1; next; }
    if (m/^108 DESTROYED/) { $self->{destroyflag} = 1; next; }
    if (m/^109 SESSIONID (\S+)/) { $self->{sessionid} = $1; next; }
    if (m/^110 CLASS (\S+)/) { push (@{$self->{classlist}}, $1); next; }
    if (m/^111 ROLLBACK$/) { $self->rollbackflag = 1; next; }
    if (m/^30[0-13-7]/) { push (@{$self->{info}}, $_); next; }
    if (m/^302 BAD DATA\s+(\d+)\s+(\S+)\s*(.*)?/) {
      my ($oid, $key, $msg) = ($1, $2, $3);
      if (!$msg) { $msg = "unknown-error"; }
      $self->{baddata}->{$oid}->{$key} = $msg;
      next;
    }
    if (m/^309 SUSPENDED\s+(.*)$/) {
      $self->{suspendedmsg} = $1 ? $self->unescape($1) : ""; next;
    }
    if (m/^2/) { $self->{success} = 1; next; }
    if (m/^4/) { $self->{success} = 0; next; }
    if ($DEBUG) { print STDERR "Did not recognize: $_"; }
    push (@info, $_); # unknown message
  }

  # compose object out of old and new data (new overrides old):
  if ($self->{destroyflag}) {
    $self->{object} = {};
  } else {
    $self->{object} = { %{$self->{old}}, %{$self->{new}} };
  }

  return $success;
}

# $ok = $cce->auth($username, $password);
sub auth
{
  my $self = shift;
  my $username = shift;
  my $password = shift;

  $self->_send("AUTH", $username, $password);
  my @msgs = $self->_recv();

  if ($DEBUG) { print STDERR $self->{success} ?
  "Authenticated\n" : "Authentication Failed.\n"; }
  return $self->{success};
}

# ($ok, $badkeys, @info) = $cce->create ($class, %object );
sub create
{
  my $self = shift || die;
  my $class = shift || die;
  my $object = shift || {};

  $self->_send("CREATE", $class, $object);
  my @msgs = $self->_recv();

  if ($DEBUG) { print STDERR $self->{success} ?
  "Created $self->{oidlist}->[0]\n" : "Creation Failed.\n"; }
  return ($self->{success},
    $self->{baddata},
    @{$self->{info}});
}

sub oid
{
  my $self = shift;
  return $self->{oidlist}->[0];
}

# $ok = $cce->authkey($username, $sessionid);
sub authkey
{
  my $self = shift;
  my $username = shift;
  my $sessionid = shift;

  $self->_send("AUTHKEY", $username, $sessionid);
  my @msgs = $self->_recv();

  if ($DEBUG) { print STDERR $self->{success} ?
    "Re-authenticated\n" : "Re-authentication Failed.\n"; }
  return $self->{success};
}

# $ok = $cce->endkey();
sub endkey
{
  my $self = shift;

  $self->_send("ENDKEY");
  my @msgs = $self->_recv();

  if ($DEBUG) { print STDERR $self->{success} ?
    "Session Closed\n" : "Session Closure Failed.\n"; }
  return $self->{success};
}

# ($ok, $auth_oid) = $cce->whoami();
sub whoami
{
  my $self = shift;

  $self->_send("WHOAMI");
  my @msgs = $self->_recv();

  if ($DEBUG) { print STDERR $self->{success} ?
    "WHOAMI Succeeded\n" : "WHOAMI Failed.\n"; }
  return ($self->{success}, @{$self->{oidlist}});
}

# ($ok, @info) = $cce->destroy ($oid);
sub destroy
{
  my $self = shift;
  my $oid = shift;

  if (!$oid) {
    print STDERR "$0: bad oid passed to cce->destroy\n";
    return 0;
  }

  $self->_send("DESTROY", $oid);
  $self->_recv();

  if ($DEBUG) { print STDERR $self->{success} ?
    "Destroyed $oid\n" : "Destruction Failed.\n"; }
  return ($self->{success}, @{$self->{info}});
}

# ($ok, $badkeys, @info) = $cce->set($oid, $namespace, \%object);
sub set
{
  my $self = shift;
  my $oid = shift;
  my $namespace = shift;
  my $object = shift || {};

  if ($namespace) { $oid .= "." . $namespace; }

  $self->_send("SET", $oid, $object);
  $self->_recv();

  if ($DEBUG) { print STDERR $self->{success} ?
    "Set succeeded\n" : "Set Failed.\n"; }
  return ($self->{success}, $self->{baddata}, @{$self->{info}});
}

# ($ok, $object, $old, $new) = $cce->get($oid, $namespace);
sub get
{
  my $self = shift;
  my $oid = shift;
  my $namespace = shift;
  my $object = {};
  my $old = {};
  my $new = {};

  if ($namespace) { $oid .= "." . $namespace; }

  $self->_send("GET", $oid);
  $self->_recv();

  if ($DEBUG>1) {
    print STDERR $self->{success} ? "Get succeeded.\n" : "Get Failed.\n";
    while (my($k,$v)=each(%{$self->{object}})) {
      print STDERR "\t$k=$v\n";
    }
  }

  return ($self->{success}, $self->{object}, $self->{old}, $self->{new});
}

# ($ok, @namelist, @info) = $cce->names($oid);
sub names
{
  my $self = shift;
  my $oid = shift;

  $self->_send("NAMES", $oid);
  $self->_recv();

  return ($self->{success}, @{$self->{namelist}}, @{$self->{info}});
}

# ($ok, @classlist, @info) = $cce->classes($oid);
sub classes
{
  my $self = shift;

  $self->_send("CLASSES");
  $self->_recv();

  return ($self->{success}, @{$self->{classlist}}, @{$self->{info}});
}

# (@oidlist) = $cce->find($class, \%criteria);
sub find
{
  my $self = shift;
  my $class = shift;
  my $crit = shift || {};

  $self->_send("FIND",$class,$crit);
  $self->_recv();

  if ($DEBUG) { print STDERR "found: @{$self->{oidlist}}\n"; }
  return (@{$self->{oidlist}});
}

# (@oidlist) = $cce->findSorted($class, $key, \%criteria);
# Find objects of type $class, matching %criteria, returning the list sorted
# alphabetically by $key
sub findSorted
{
  my $self = shift;
  my $class = shift;
  my $key = shift;
  my $crit = shift || {};

  $self->_send("FIND", $class, "SORTTYPE", "ascii", "SORTPROP", $key, $crit);
  $self->_recv();

  if ($DEBUG) { print STDERR "found: @{$self->{oidlist}}\n"; }
  return (@{$self->{oidlist}});
}

# (@oidlist) = $cce->findNSorted($class, $key, \%criteria);
# Find objects of type $class, matching %criteria, returning the list sorted
# numerically by $key
sub findNSorted
{
  my $self = shift;
  my $class = shift;
  my $key = shift;
  my $crit = shift || {};

  $self->_send("FIND", $class, "SORTTYPE", "old_numeric", "SORTPROP",
    $key, $crit);
  $self->_recv();

  if ($DEBUG) { print STDERR "found: @{$self->{oidlist}}\n"; }
  return (@{$self->{oidlist}});
}

# this is a helper function needed by findx()
sub _regex_crit
{
  my $self = shift;
  my $hash = shift;
  my @encoded;
  my ($key,$val);

  while (($key,$val) = each %$hash) {
    push (@encoded, $key);
    push (@encoded, "~");
    push (@encoded, $val);
  }
  return @encoded;
}

# (@oidlist) = $cce->findx($class, \%match, \%rematch, $sorttype, $sortprop);
# Find objects of type $class, matching %match exactly, matching %rematch
#  by regex, returning the list sorted by $sorttype on key $sortprop
sub findx
{
  my $self = shift;
  my $class = shift;
  my $crit = shift || {};
  my $recrit = shift || {};
  my $stype = shift;
  my $sprop = shift;

  if ($stype && $sprop) {
    $self->_send("FIND", $class, $crit, $self->_regex_crit($recrit),
      "SORTTYPE", $stype, "SORTPROP", $sprop);
  } else {
    $self->_send("FIND", $class, $crit, $self->_regex_crit($recrit));
  }

  $self->_recv();

  if ($DEBUG) { print STDERR "found: @{$self->{oidlist}}\n"; }
  return (@{$self->{oidlist}});
}

# $ok = $cce->bye("fail", "msg");
sub bye
{
  my $self = shift;
  my $state = shift;

  if (@_) { $self->warn(@_); }

  if ($state) {
    $self->_send("BYE", $state);
  } else {
    $self->_send("BYE");
  }
  $self->_recv();

  $self->_disconnect();

  return $self->{success};
}

sub begin
{
  my $self = shift;

  $self->_send("BEGIN");
  $self->_recv();

  return $self->{success};
}

# ($ok, @info) = $cce->commit($oid);
sub commit
{
  my $self = shift;

  $self->_send("COMMIT");
  $self->_recv();

  return ($self->{success}, @{$self->{info}});
}

# ($ok) = $cce->suspend($reason);
sub suspend
{
  my $self = shift;
  my $reason = shift;

  if (defined($reason)) {
    $self->_send("ADMIN", "SUSPEND", $self->_escape($reason));
  } else {
    $self->_send("ADMIN", "SUSPEND");
  }
  $self->_recv();

  return $self->{success};
}

# ($ok) = $cce->resume();
sub resume
{
  my $self = shift;

  $self->_send("ADMIN", "RESUME");
  $self->_recv();

  return $self->{success};
}

sub baddata
{
  my $self = shift;
  my $oid = shift || $self->{event_oid};
  my $key = shift;
  my $value = $self->msg_create(@_);

  $self->_send("BADDATA",$oid,$key,$value);
  $self->_recv();

  return $self->{success};
}

sub info
{
  my $self = shift;
  my $msg = $self->msg_create(@_);

  $self->_send("INFO",$msg);
  $self->_recv();

  return $self->{success};
}

sub warn
{
  my $self = shift;
  my $msg = $self->msg_create(@_);

  $self->warn_raw($msg);
}

sub warn_raw
{
  my $self = shift;
  my $msg = shift;

  if( $DEBUG ) {
    print STDERR "> WARN $msg\n";
  }

  $self->_send("WARN",$msg);
  $self->_recv();

  return $self->{success};
}

sub msg_create
{
  my $self = shift;

  my $msg = shift || return '';
  if ($msg =~ m/^\s*\[\[/) { return $msg; }

  my $vars = shift || {};

  my $domain = $self->{domain} || return "\"$msg\"";
  if ($msg =~ s/^(\S+?)\.//) { $domain = $1; }

  $msg = "[[$domain\.$msg";

  while ( my ($key,$val) = each %{$vars} ) {
    $msg .= ',' . $self->_escape($key) . '=' . $self->_escape($val);
  }

  $msg .= "]]";
  #$msg = $self->_escape($msg);
  #print STDERR $msg;

  return $msg;
}

# validate
#
# a helper function to help prettify the code used to validate
# data passed to the handler.
#
# returns: 1 on failure, 0 on success (ie. "error count").
sub validate
{
  return 0;
}

# pack and unpack arrays
sub array_to_scalar
{
  my $self = shift;

  my $scalar = "&";
  while (defined($_ = shift)) {
    s/([^A-Za-z0-9_\. -])/sprintf("%%%02X",ord($1))/ge;
    s/ /+/g;
    $scalar .= $_ . '&';
  }

  if ($scalar eq "&") { $scalar = ""; } # special case

  return $scalar;
}

sub scalar_to_array
{
  my $self = shift;
  my $scalar = shift || "";
  $scalar =~ s/^&//;
  $scalar =~ s/&$//;
  my @data = split(/&/, $scalar);
  for ($i = 0; $i <= $#data; $i++) {
    $data[$i] =~ s/\+/ /g;
    $data[$i] =~ s/%([0-9a-fA-F]{2})/chr(hex($1))/ge;
  }
  return @data;
}

# Helper functions to gmake validating arrays and hashes look less messy.

sub validate_array
{
  return 0;
}

sub validate_hash
{
  return 0;
}


1;

__END__

=head1 NAME

CCE - Cobalt Configuration Engine client library

=head1 SYNOPSIS

    use CCE;
    my $cce = new CCE;
    $cce->connectfd();
      or
    $cce->connectuds();

=head1 ABSTRACT

This perl library implements an object oriented interface for communicating
with the Cobalt Configuration Engine daemon.  The same interface is used
for communicating with the daemon both in the context of a management
client and in the context of a triggered event handler.

=head1 API

=head2 CREATING A NEW CONNECTION HANDLE

    my $cce = new CCE;

=head2 CONNECTING TO THE DAEMON

The CCE object supports two ways of connecting to the daemon.  The first is
to open a new unix-domain socket connection to the database:

    $cce->connectuds( $filename );

If "$filename" is omitted, cce will instead attempt to connect to the
default path: "(prefix)/raqdevil/cced.socket".

When an event handler is run, the connection to the CCE daemon already
exists.  For this case, an alternate API exists to initialize a CCE
session:

    $cce->connectfd();

=head2 AUTH

    $ok = $cce->auth( $username, $password );

This checks that a User object exists with that username, that the
enabled property of that object is true, and then attempts to validate
the username and password against PAM.

=head2 AUTHKEY

    $ok = $cce->authkey( $username, $sessionid );

This authenticates as $username after verifying the $sessionid is one which
has been returned from a successful authentication to $username.
Session ids timeout after 1 hour.

=head2 ENDKEY

    $ok = $cce->endkey();

This will terminate any current authentication and gmake the sessionid
for the current session invalid.

=head2 WHOAMI

    ($ok, $auth_oid) = $cce->whoami();

The returns the object id of the currently authenticated user.
This is -1 if there is no current authentication (if endkey has been used,
or if auth/authkey has never been called)

=head2 CREATE

    ($ok, $badkeys, @info) = $cce->create( $class, \%object );

Creates a new object of the specified class $class, initialized
using the attributes specified in the %object hash.

The OID of the object just created can be returned by a subsequent
call to $cce->oid();

=over 4

=item $ok indicates whether the operation was successful.

=item %$badkeys is a hash where the key is the oid and the value is a hash.
The key of the secondary hash is the name of attribute whose value was
rejected, and the value is the explanation of why that attribute was
rejected.

=item @info is a list of additional messages returned by the operation.

=back

=head2 DESTROY

    ($ok, @info) = $cce->destroy( $oid );

Destroys the specified object.

=over 4

=item $ok indicates whether the operation was successful.

=item @info is a list of additional messages returned by the operation.

=back

=head2 SET

    ($ok, $badkeys, @info) = $cce->set( $oid, $namespace, \%object );

Changes the attributes of an existing object.  $oid is the numeric
id of the object to modify, $namespace specifies which namespace of
the object to operate on, and %object is a hash of attributes to change.

If namespace is omitted, the default main namespace ("") will be used
instead.

=over 4

=item $ok indicates whether the operation was successful.

=item %$badkeys is a hash where the key is the oid and the value is a hash.
The key of the secondary hash is the name of attribute whose value was
rejected, and the value is the explanation of why that attribute was
rejected.

=item @info is a list of additional messages returned by the operation.

=back

=head2 GET

    ($ok, $object, $old, $new) = $cce->get( $oid, $namespace );

Get is used to fetch all of the attributes of an object within a
single namespace.  $oid is the numeric id of the object,
and $namespace specifies which namespace of attributes to fetch.

If namespace is omitted, the default main namespace ("") will be used
instead.

=over 4

=item $ok indicates whether the operation was successful.

=item %$object is a hash of the attributes of the object within the
specified namespace.

=item %$old is a hash of the previous values of the attributes of the
object within the specified namespace.

=item %$new is a hash of only the attributes that have changed in
the course of the current transaction.

=back

In the case of a client communicating with the CCE daemon, the %$object
and %$old hashes will always be identical, and the %$new hash will
always be empty.

In the case of an event handler communicating with the CCE daemon, the %$old
contains the attributes of the object before the start of the current
transaction.  %$new contains only the attributes that have changed in the
current transaction.  %$current contains the most up-to-date version of
all attributes for the object.

=head2 NAMES

    ($ok, @namelist, @info) = $cce->names($oid);

Returns the names of all valid namespaces associated with an object.

=over 4

=item $ok indicates whether the operation was successful.

=item @$namelist is a list of the names of all valid namespaces.

=item @info is a list of additional messages returned by the operation.

=back

=head2 CLASSES

    ($ok, @classlist, @info) = $cce->classes();

Returns the list of all the classes known to CCE.

=head2 FIND

    @oidlist = $cce->find($class, \%criteria)

The find function searches the database for all objects of class "$class"
with attributes that match those specified in %criteria.

If criteria is omited, all objects of the specified class are returned.

=over 4

=item @oidlist is a list of numeric object identifiers.

=back

=head2 FINDX

    @oidlist = $cce->findx($class, \%match, \%rematch, $sorttype, $sortprop);

Find objects of type $class, matching the criteria in %match exactly,
matching the criteria in \%rematch by regex, and returns the list sorted by
$sorttype on the property in key $sortprop.

=head2 BEGIN

    $ok = $cce->begin();

This initiates the delayed-handler state.

=head2 COMMIT

    ($ok, @info) = $cce->commit();

This is valid only after a previous call to begin().  It runs all the
handlers that have been triggered since the call to begin, and returns
a success code based on the success or failure of them all.  It also
returns all info/warn/baddata messages from the handlers.

=head2 SUSPEND

    ($ok) = $cce->suspend($message);

This puts CCE into a suspended state that will prevent any changes being
made.  All operations will fail with a suspended error code.
It takes an optional message that will be shown with the error code.
This requires authentication as a system administrator.

=head2 RESUME

    ($ok) = $cce->resume($message);
This is valid after a suspend() call to move back to a normal state.
This requires authentication as a system administrator.

=head2 SUSPENDED

    ($message) = $cce->suspended();

This returns undef if CCE is not suspended, and returns the message given
to the suspend() call if it is.

=head2 BYE

    $ok = $cce->bye( $success, $msg )

Says goodbye to the server and terminates the connection.  For clients,
$success and $msg are meaningless.  In the context of an event handler,
$success should contain either the strings "success" or "fail" to
indicate whether the handler succeeded or failed.  $msg should then
contain an arbitrary string used to elucidate the exit code.

=over 4

=item $ok is true if the bye command was successful, which it always is.

=back

=head2 BADDATA

    $ok = $cce->baddata ($oid, $key, @MSG)

Only used by event handlers: emits a message back to the server indicating
that the property $key of object $oid was invalid for the reason specified
in $value.

=over 4

=item $ok is true if the command is successful.
=item @MSG: see below.

=back

=head2 INFO

    $ok = $cce->info (@MSG)

Emits an arbitrary message back to the server.

=item @MSG: see below.

=head2 WARN

    $ok = $cce->warn (@MSG)

Emits an arbitrary warning message back to the server.

=item @MSG: see below.

=head2 @MSG

@MSG contains information used to construct an i18n tag.  It can take
one of two forms.  The first form is a fully-formed i18n tag string:

    [[myDomain.foo,var=val]]

Fully-formed i18n tags of this form are simply passed through unmolested.

Otherwise, the $MSG[0] is used as the tag name, and $MSG[1] contains
a hash of variables.

=head2 ARRAY_TO_SCALAR

    $scalar = $cce->array_to_scalar(@array);

This converts from a perl array datatype to a CCE scalar encoded array
suitable for a property of "array=1" type.

=head2 SCALAR_TO_ARRAY

    @array = $cce->scalar_to_array($scalar);

This converts from a CCE scalar encoded array to a perl array datatype.
This is useful for interpreting the data stored in a property of "array=1"
type.

=cut

# Copyright (c) 2003 Sun Microsystems, Inc. All  Rights Reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# -Redistribution of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# -Redistribution in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# Neither the name of Sun Microsystems, Inc. or the names of contributors may
# be used to endorse or promote products derived from this software without
# specific prior written permission.
#
# This software is provided "AS IS," without a warranty of any kind. ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT, ARE HEREBY EXCLUDED. SUN MICROSYSTEMS, INC. ("SUN") AND ITS LICENSORS SHALL NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF USING, MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES. IN NO EVENT WILL SUN OR ITS LICENSORS BE LIABLE FOR ANY LOST REVENUE, PROFIT OR DATA, OR FOR DIRECT, INDIRECT, SPECIAL, CONSEQUENTIAL, INCIDENTAL OR PUNITIVE DAMAGES, HOWEVER CAUSED AND REGARDLESS OF THE THEORY OF LIABILITY, ARISING OUT OF THE USE OF OR INABILITY TO USE THIS SOFTWARE, EVEN IF SUN HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
#
# You acknowledge that  this software is not designed or intended for use in the design, construction, operation or maintenance of any nuclear facility.
