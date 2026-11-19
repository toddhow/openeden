#ifndef OPENMW_MWWORLD_NEUTRALNAVTHREADS_H
#define OPENMW_MWWORLD_NEUTRALNAVTHREADS_H

#include <cstddef>

namespace MWWorld
{
    /// Task 0357: the number of navmesh updater threads. A session with a game pack's neutral world uses its own
    /// count (Skyrim's cold exterior navmesh waits seconds on one thread, task 0323); every other session keeps the
    /// normal one. Never below 1.
    inline std::size_t chooseNavMeshUpdaterThreads(bool neutralWorld, std::size_t normal, std::size_t neutral)
    {
        const std::size_t chosen = neutralWorld ? neutral : normal;
        return chosen < 1 ? 1 : chosen;
    }
}

#endif
