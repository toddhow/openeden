#!/bin/bash
# Fails when a C or C++ file under components/ or apps/ includes a header its layer may not include
# (task 0081; docs/design/game-packs.md section 2). The layers, the directories in each, who may include whom and the
# list of violations that exist today are all in CI/layer_rules.txt. The exceptions list may shrink and never grow: a
# violation that is not listed fails, and so does a listed pair that no longer occurs (a stale exception).
#
# Comments and strings do not count (an include in a comment is not one). An include written in quotes is read from the
# original line, as in CI/check_no_osg.sh. Needs bash, git and perl only; it reads no file but the tracked ones.
#
# LAYER_RULES_FILE names another rule file (the self-test CI/test_layer_rules.sh uses it).
# LAYER_RULES_MATRIX=1 prints, instead of failing, the include lines counted per pair of layers.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
rules="${LAYER_RULES_FILE:-CI/layer_rules.txt}"
[ -f "$rules" ] || { echo "check_layer_rules: $rules not found"; exit 1; }

git ls-files | LAYER_RULES="$rules" perl -e '
    use strict;
    my $rulesFile = $ENV{LAYER_RULES};
    my (@layers, %dir, %allow, @deny, %except);
    open(my $r, "<", $rulesFile) or die "$rulesFile: $!";
    my $n = 0;
    while (my $l = <$r>) {
        $n++;
        $l =~ s/\r?\n$//;
        next if $l =~ /^\s*(#.*)?$/;
        if ($l =~ /^layers\s+(.+)$/) { @layers = split " ", $1; }
        elsif ($l =~ m{^dir\s+(\S+)\s+(\S+)\s*$}) { $dir{$1} = $2; }
        elsif ($l =~ /^allow\s+(\S+)((?:\s+\S+)*)\s*$/) { $allow{$1} = { map { $_ => 1 } split " ", $2 }; $allow{$1}{$1} = 1; }
        elsif ($l =~ m{^deny\s+(\S+)\s+(\S+)\s*$}) { push @deny, [$1, $2]; }
        elsif ($l =~ m{^except\s+(\S+)\s+->\s+(\S+)\s*#\s*(\S.*)$}) {
            die "$rulesFile:$n: duplicate exception $1 -> $2\n" if exists $except{"$1 -> $2"};
            $except{"$1 -> $2"} = 0;
        }
        else { die "$rulesFile:$n: cannot read: $l\n"; }
    }
    close $r;
    my %isLayer = map { $_ => 1 } @layers;
    for my $p (keys %dir) { die "$rulesFile: dir $p names an unknown layer $dir{$p}\n" unless $isLayer{$dir{$p}}; }
    for my $k (keys %allow) { die "$rulesFile: allow names an unknown layer $k\n" unless $isLayer{$k}; }
    for my $l (@layers) { die "$rulesFile: layer $l has no allow line\n" unless $allow{$l}; }
    my @prefixes = sort { length($b) <=> length($a) } keys %dir;
    sub layerOf {
        my ($path) = @_;
        for my $p (@prefixes) { return $dir{$p} if $path eq $p || index($path, "$p/") == 0; }
        return undef;
    }

    my (@files, %tracked);
    while (my $f = <STDIN>) { chomp $f; $tracked{$f} = 1; push @files, $f; }

    my $failed = 0;
    my (%matrix, %unclassified);
    my $normalise = sub {
        my @out;
        for my $part (split m{/}, $_[0]) {
            if ($part eq "..") { return undef unless @out; pop @out; }
            elsif ($part ne "." && $part ne "") { push @out, $part; }
        }
        return join "/", @out;
    };
    for my $file (@files) {
        next unless $file =~ m{\.(c|cc|cpp|cxx|c\+\+|h|hh|hpp|hxx|h\+\+|inl|inc|ipp|tpp|txx|mm|m)(\.in|\.cmake)?$};
        next unless $file =~ m{^(components|apps)/};
        my $from = layerOf($file);
        if (!defined $from) {
            my ($top) = $file =~ m{^((?:components|apps)/[^/]+)};
            $unclassified{$top} = 1;
            next;
        }
        open(my $h, "<:raw", $file) or die "$file: $!";
        local $/;
        my $original = <$h>;
        close $h;
        next unless $original =~ /#\s*include/;
        my $text = $original;
        # Blank comments, raw strings, strings and character literals, keeping the line count (as check_no_osg.sh).
        $text =~ s{R"([^(\s"\\]{0,16})\(.*?\)\1"|//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"|(?<![0-9A-Za-z_])\x27(?:[^\x27\\\n]|\\.){1,10}\x27}{
            my $m = $&;
            my $newlines = $m =~ s/[^\n]//gr;
            $m =~ /^R?"/ ? "\"\"" . $newlines : $newlines;
        }gse;
        my @original = split /\n/, $original;
        (my $dirOfFile = $file) =~ s{/[^/]*$}{};
        my $line = 0;
        for (split /\n/, $text) {
            $line++;
            next unless /^\s*#\s*include\b/;
            my $source = $original[$line - 1] // "";
            $source =~ s/\r$//;
            next unless $source =~ /^\s*#\s*include\s*([<"])([^>"]+)[>"]/;
            my ($open, $name) = ($1, $2);
            # Resolve the include to a repository path. The include roots are the repository root, the including
            # directory (quotes only) and the including application (apps/openmw, apps/opencs, ...).
            my @candidates;
            push @candidates, $name if $name =~ m{^(components|apps)/};
            push @candidates, "$dirOfFile/$name" if $open eq "\"";
            push @candidates, "$1/$name" if $file =~ m{^(apps/[^/]+)/};
            my $target;
            for my $c (@candidates) {
                my $p = $normalise->($c);
                if (defined $p && $tracked{$p}) { $target = $p; last; }
            }
            # A generated or untracked header under components/ or apps/ is classified by its written path.
            $target //= $name if $name =~ m{^(components|apps)/};
            next unless defined $target && $target =~ m{^(components|apps)/};
            my $to = layerOf($target);
            if (!defined $to) {
                my ($top) = $target =~ m{^((?:components|apps)/[^/]+)};
                $unclassified{$top} = 1;
                next;
            }
            my $rule = "";
            for my $d (@deny) {
                my ($a, $b) = @$d;
                if (index($file, $a) == 0 && index($target, $b) == 0) { $rule = "deny $a -> $b"; last; }
            }
            $rule = "layer $from may not include layer $to" if $rule eq "" && !$allow{$from}{$to};
            $matrix{"$from -> $to"}++;
            next if $rule eq "";
            my $key = "$file -> $target";
            if (exists $except{$key}) { $except{$key}++; next; }
            $matrix{"VIOLATION $from -> $to"}++;
            print "$file:$line: $rule: $target\n" unless $ENV{LAYER_RULES_MATRIX};
            $failed = 1;
        }
    }
    if ($ENV{LAYER_RULES_MATRIX}) {
        print "$matrix{$_} $_\n" for sort keys %matrix;
        exit 0;
    }
    for my $top (sort keys %unclassified) {
        print "$top: not in $rulesFile (add a dir line)\n";
        $failed = 1;
    }
    for my $key (sort keys %except) {
        next if $except{$key} > 0;
        print "$rulesFile: stale exception, no longer occurs or no longer a violation: $key\n";
        $failed = 1;
    }
    my $lines = 0;
    $lines += $_ for values %except;
    printf "check_layer_rules: %d exceptions (%d include lines)\n", scalar(keys %except), $lines;
    exit $failed;
' || { echo "check_layer_rules: see above. Layers and exceptions are in $rules (task 0081); an exception may be removed, never added."; exit 1; }
