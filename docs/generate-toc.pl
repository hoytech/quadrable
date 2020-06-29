#!/usr/bin/env perl

use common::sense;

my $lines = '';
my $headers = [];
my $seenHeaders = {};

{
    open(my $fh, '<', 'README.md') || die "unable to open README.md: $!";

    while (<$fh>) {
        next if /^<!-- START OF TOC -->/ .. /^<!-- END OF TOC -->/;

        $lines .= $_;

        if (/^[#]+ (.*)/) {
            die "duplicate header: $1" if $seenHeaders->{$1};
            $seenHeaders->{$1}++;
            push @$headers, $&;
        }
    }
}

my $toc = '';

for my $header (@$headers) {
    $header =~ /^(#+) (.*)/;
    my $prefix = $1;
    my $title = $2;

    $prefix =~ s/^##//;
    $prefix =~ s/^\s+//;
    $prefix =~ s/#/  /g;
    $prefix = "$prefix*";

    my $link = lc $title;
    $link =~ s/\s+/-/g;
    $toc .= "$prefix [$title](#$link)\n";
}

{
    open(my $ofh, '>', 'README.md.tmp') || die "unable to open README.md: $!";

    $lines =~ s{<!-- TOC FOLLOWS -->}{<!-- TOC FOLLOWS -->\n<!-- START OF TOC -->\n$toc<!-- END OF TOC -->\n};

    print $ofh $lines;
}

system("mv -f README.md.tmp README.md");
