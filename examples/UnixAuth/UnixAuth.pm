package UnixAuth;

use strict;
use vars qw($VERSION @ISA @EXPORT @EXPORT_OK);

require Exporter;
require DynaLoader;
require AutoLoader;

@ISA = qw(Exporter DynaLoader);
# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.
@EXPORT = qw(

);
$VERSION = '1.0';

bootstrap UnixAuth $VERSION;

# Preloaded methods go here.

# Autoload methods go after =cut, and are processed by the autosplit program.

1;
__END__
# Below is the stub of documentation for your module. You better edit it!

=head1 NAME

UnixAuth - Perl extension for sending a msg with cmsgcred authentication.

=head1 SYNOPSIS

  use UnixAuth;

=head1 DESCRIPTION

Stub documentation for UnixAuth was created by h2xs. It looks like the
author of the extension was negligent enough to leave the stub
unedited.

Blah blah blah.

Damn right.

=head1 AUTHOR

Devon H. O'Dell, dodell@offmyserver.com

=head1 SEE ALSO

perl(1).

=cut
