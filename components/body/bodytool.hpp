#ifndef OPENEDEN_COMPONENTS_BODY_BODYTOOL_H
#define OPENEDEN_COMPONENTS_BODY_BODYTOOL_H

// OpenEden AdvancedBodies, stage B1 (task 0063): the command-line validator as a function, so that it can be tested
// without starting a program. apps/edenbodyvalidate/main.cpp only forwards argv and the standard streams.

#include <iosfwd>
#include <string>
#include <vector>

namespace Body
{
    /// Usage: edenbodyvalidate [--json] <package>
    /// <package> is a body.json file, a package folder (holding body.json) or a .edenpak (holding "body.json").
    /// Files the package names (a skeleton profile path, rig and profile paths) are read relative to the folder or
    /// from the pack. The text report (or the canonical JSON report with --json) goes to `out`. Problems that stop
    /// the check (no such file, not a package, a body.json that fails to load: the located error) go to `err`.
    /// Returns 0 when the report has no errors (warnings are fine), 1 when it has errors OR the body.json fails to
    /// load as a package, 2 when the input cannot be read at all or the arguments are wrong. Never throws.
    int runBodyValidateTool(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
}

#endif
