#ifndef OPENEDEN_COMPONENTS_ANIMTRACE_TRACETOOL_H
#define OPENEDEN_COMPONENTS_ANIMTRACE_TRACETOOL_H

// OpenEden, CreativeMotion in the game, stage 0 (task 0067): the command-line measurement as a function so that it
// can be tested without starting a program. apps/creativemotioncheck/main.cpp only forwards argv and the streams.

#include <iosfwd>
#include <string>
#include <vector>

namespace AnimTrace
{
    /// Usage: creativemotioncheck [--json] [--ref <actor>] [--label <text>] [--window <frames>]
    ///                            [--meta <key>=<value>]... [--summary] <openmw.log>...
    /// Reads the CMTRACE and CMPHASE lines of the log and writes the measurements of every scripted event and every
    /// change of a bone group's animation, as a table or (--json) as canonical JSON, to `out`. --meta adds a key and
    /// value to the report (the build's commit and time, the extra settings). --summary takes several logs (runs of one
    /// variant) and writes the range of each measure over them. Problems go to `err`. Returns 0 on success, 1 when a
    /// log holds no trace lines of the actor, 2 when the arguments are wrong or a file cannot be read. Never throws.
    int runTraceTool(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
}

#endif
