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
            my $whole = $&;
            my $title = $1;

            my $link = title2link($1);
            die "duplicate header: $link" if $seenHeaders->{$link};
            $seenHeaders->{$link}++;
            push @$headers, $whole;
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

    my $link = title2link($title);
    $toc .= "$prefix [$title](#$link)\n";
}

{
    open(my $ofh, '>', 'README.md.tmp') || die "unable to open README.md: $!";

    $lines =~ s{<!-- TOC FOLLOWS -->}{<!-- TOC FOLLOWS -->\n<!-- START OF TOC -->\n$toc<!-- END OF TOC -->};

    print $ofh $lines;
}


while ($lines =~ m{\[.*?\][(]#(.*?)[)]}g) {
    my $link = $1;
    if (!$seenHeaders->{$link}) {
        print STDERR "WARNING: Unresolved link: $link\n";
    }
}

system("mv -f README.md.tmp README.md");



sub title2link {
    my $title = shift;
    my $link = lc $title;
    $link =~ s/\s+/-/g;
    $link =~ s/[+]//g;
    return $link;
}
