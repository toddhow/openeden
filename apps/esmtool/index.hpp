#ifndef OPENMW_ESMTOOL_INDEX_H
#define OPENMW_ESMTOOL_INDEX_H

#include "arguments.hpp"

namespace EsmTool
{
    /// The `index` mode (task 0083): reads the header and the record index of a plugin of either family through
    /// Content::Plugin and prints aggregates only (counts per record type, flags): no record names or text.
    /// Returns the process exit code.
    int index(const Arguments& info);
}

#endif
