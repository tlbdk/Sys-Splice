use strict;
use warnings;
use Module::Build;

die 'OS unsupported! Patches welcome :)' unless $^O =~ /linux/i;

my $builder = Module::Build->new(
    module_name        => 'Sys::Splice',
    license            => 'perl',
    dist_author        => 'Troels Liebe Bentsen <troels@infopro.dk>',
    dist_version_from  => 'lib/Sys/Splice.pm',
    create_readme      => 1,
    create_makefile_pl => 'passthrough',
    requires => {
        'perl'   => '>= 5.8.0',
        'POSIX'  => 0,
        'Config' => 0,
    },
    build_requires => {
        'Test::More' => 0,
    },
    add_to_cleanup => [ 
        'Sys-Prctl-*',
        'Makefile',
        'blib',
    ],
);

$builder->create_build_script();
