#! /usr/bin/perl

use strict;
use warnings;

use File::Path 'make_path';

my ( $dest_dir, @files ) = @ARGV;
exit unless @files;

foreach my $file (@files) {
    if ( $file =~ m{/([^./]*)\.(..)[^.]*.mo$} ) {
        make_path( "$dest_dir/$2/LC_MESSAGES", { errno => \my $error } );

        my $new_file = "$dest_dir/$2/LC_MESSAGES/$1.mo";

        open( my $fd_in, '<', $file ) or die "Can't open $file because $!";
        open( my $fd_out, '>', $new_file ) or die "Can't open $new_file because $!";

        while ( sysread $fd_in, my $buffer, 4096 ) {
            syswrite $fd_out, $buffer;
        }

        close $fd_in;
        close $fd_out;
    }
}

