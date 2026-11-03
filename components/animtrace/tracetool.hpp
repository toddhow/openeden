#ifndef OPENEDEN_COMPONENTS_ANIMTRACE_TRACETOOL_H
#define OPENEDEN_COMPONENTS_ANIMTRACE_TRACETOOL_H

// OpenEden, CreativeMotion in the game, stage 0 (task 0067): the command-line measurement as a function so that it
// can be tested without starting a program. apps/creativemotioncheck/main.cpp only forwards argv and the streams.

#include <iosfwd>
#include <string>
#include <vector>

namespace AnimTrace
{
    /// Usage: creativemotioncheck [--json] [--ref <actor>] [--label <text>] [--window <frames>] <openmw.log>
    /// Reads the CMTRACE and CMPHASE lines of the log and writes the measurements of every scripted event, as a
    /// table or (--json) as canonical JSON, to `out`. Problems go to `err`. Returns 0 on success, 1 when the log
    /// holds no trace lines of the actor, 2 when the arguments are wrong or the file cannot be read. Never throws.
    int runTraceTool(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
}

#endif
