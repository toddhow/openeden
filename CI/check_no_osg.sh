#!/bin/bash
# Fails if any C or C++ file in the repository goes back to OpenSceneGraph (Track V, decision 0002; tasks 0013 and 0014):
# an include of an OSG header (osg, osgDB, osgUtil, osgQt, OpenThreads and the rest), a name in an osg* namespace
# or OpenThreads (osg::, osgDB::, OpenThreads::, ...), a declaration of such a namespace, or an OSG_ macro. It also fails on `*=` whose right-hand side is written as a
# VSG vector, which is a component-wise product in VSG and has no meaning from OSG; a named vector on the right cannot be
# told from a scalar by text alone, so that part is a best effort.
#
# Comments and strings do not count: scripts read the old type names as strings (components/lua/utilpackage.cpp keeps
# them, and Lua tests assert them), and comments record where a value or a rule came from.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

git ls-files | grep -E '\.(c|cc|cpp|cxx|c\+\+|h|hh|hpp|hxx|h\+\+|inl|inc|ipp|tpp|txx|mm|m)(\.in|\.cmake)?$' | perl -e '
    use strict;
    my $failed = 0;
    while (my $file = <STDIN>) {
        chomp $file;
        open(my $h, "<:raw", $file) or die "$file: $!";
        local $/;
        my $original = <$h>;
        close $h;
        my $text = $original;
        # Blank comments, raw strings, strings and character literals, keeping the line count. A character literal
        # holding a double quote must not start a string; a digit separator (1\x27000) is not a character literal.
        $text =~ s{R"([^(\s"\\]{0,16})\(.*?\)\1"|//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"|(?<![0-9A-Za-z_])\x27(?:[^\x27\\\n]|\\.){1,10}\x27}{
            my $m = $&;
            my $newlines = $m =~ s/[^\n]//gr;
            $m =~ /^R?"/ ? "\"\"" . $newlines : $newlines;
        }gse;
        # An include written in quotes was just blanked, so includes are read from the original line.
        my @original = split /\n/, $original;
        my $line = 0;
        for (split /\n/, $text) {
            $line++;
            my $source = $original[$line - 1] // "";
            my $include = /^\s*#\s*include\b/
                && $source =~ /^\s*#\s*include\s*[<"](?:[^>"]*\/)?(?:osg[A-Za-z]*|OpenThreads)(?:\/|\.h\b|\.hpp\b|>)/;
            my $problem = $include                                   ? "OSG include"
                : /\bnamespace\s+(?:osg[A-Za-z]*|OpenThreads)\b/     ? "OSG namespace"
                : /\b(?:osg[A-Za-z]*|OpenThreads)::/                 ? "OSG name"
                : /\bOSG_[A-Z0-9_]+\b/                               ? "OSG macro"
                : /\*=\s*vsg::[di]?vec[234]\b/                       ? "*= with a vector right-hand side"
                :                                                      "";
            next unless $problem;
            print "$file:$line: $problem: $source\n";
            $failed = 1;
        }
    }
    exit $failed;
' || { echo "check_no_osg: see above. OpenSceneGraph was removed in Track V (tasks 0013 and 0014)."; exit 1; }
