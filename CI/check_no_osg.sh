#!/bin/bash
# Fails if engine code goes back to OSG's math (task 0013, Track V stage V5): an <osg/...> include or an osg:: math type in
# apps/openmw, components (except CreativeMotion's copy) or apps/components_tests. It also fails on `*=` whose right-hand
# side is written as a VSG vector, which is a component-wise product in VSG and has no meaning from OSG; a named vector on
# the right cannot be told from a scalar by text alone, so that part is a best effort.
#
# Comments and strings do not count: scripts read the old type names as strings (components/lua/utilpackage.cpp keeps them),
# and comments record where a value came from. The files left for task 0014 are skipped; they are listed between the
# task-0014-osg-files markers in docs/tasks/0014-vulkan-v6-editor-tools-osg-removal.md.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

export LISTED="$(sed -n '/<!-- task-0014-osg-files -->/,/<!-- \/task-0014-osg-files -->/p' \
    docs/tasks/0014-vulkan-v6-editor-tools-osg-removal.md | tr -d '\r' | sed -n 's/^- `\(.*\)`$/\1/p')"

git ls-files -- apps/openmw components apps/components_tests ':(exclude)components/creativemotion' \
    | grep -E '\.(cpp|hpp|h|inl|inc)$' | perl -e '
    use strict;
    my %listed = map { $_ => 1 } split /\n/, $ENV{LISTED};
    my $types = qr/Vec[234][bsifdu]?|Vec[234]|Quat|Matrix[fd]?|Plane|BoundingBox[fd]?|BoundingBoxImpl|BoundingSphere[fd]?|BoundingSphereImpl|PI(?:_2|_4)?|PIf|inDegrees|RadiansToDegrees|DegreesToRadians|equivalent|sign|signOrZero|round|componentMultiply|componentDivide|[XYZ]_AXIS/;
    my $failed = 0;
    while (my $file = <STDIN>) {
        chomp $file;
        next if $listed{$file};
        open(my $h, "<:raw", $file) or die "$file: $!";
        local $/;
        my $text = <$h>;
        close $h;
        # Blank comments and strings, keeping the line count.
        $text =~ s{//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"}{
            my $m = $&;
            substr($m, 0, 1) eq "\"" || $m =~ /^\/\// ? ($m =~ /^"/ ? "\"\"" : "") : ($m =~ tr/\n//c, $m =~ s/[^\n]//gr)[1];
        }gse;
        # An include is a preprocessor line whose path was just blanked if written in quotes; check it from the original too.
        my $line = 0;
        for (split /\n/, $text) {
            $line++;
            my $problem = /^\s*#\s*include\s*<osg\//                 ? "OSG include"
                : /\bosg::(?:$types)\b/                              ? "OSG math type"
                : /\*=\s*vsg::[di]?vec[234]\b/                       ? "*= with a vector right-hand side"
                :                                                      "";
            next unless $problem;
            print "$file:$line: $problem: $_\n";
            $failed = 1;
        }
    }
    exit $failed;
' || { echo "check_no_osg: see above. Task 0013 removed OSG math from these directories."; exit 1; }
