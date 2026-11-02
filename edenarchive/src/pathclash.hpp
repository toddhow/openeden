// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_SRC_PATHCLASH_HPP
#define EDENARCHIVE_SRC_PATHCLASH_HPP

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "utf8.hpp"

namespace EdenArchive
{
    /// Finds a path that is also a directory of another path (a file `a` and a file `a/b`), section 7.3.2.
    /// `count` paths are given by `pathAt(i)`, sorted as comparePaths sorts them. Returns the index of the first such
    /// path, or std::nullopt. Allocates one string (the longest path plus a slash) and nothing per path: the paths
    /// that lie below `a/` follow `a/` in the sorted order, so one binary search per path finds them.
    template <class PathAt>
    std::optional<std::size_t> findFileDirectoryClash(std::size_t count, PathAt pathAt)
    {
        std::string probe;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::string_view path = pathAt(i);
            probe.assign(path);
            probe.push_back('/');
            std::size_t low = i + 1; // anything below `path/` sorts after `path` itself
            std::size_t high = count;
            while (low < high)
            {
                const std::size_t middle = low + (high - low) / 2;
                if (comparePaths(pathAt(middle), probe) < 0)
                    low = middle + 1;
                else
                    high = middle;
            }
            if (low < count && pathAt(low).starts_with(probe))
                return i;
        }
        return std::nullopt;
    }
}

#endif
