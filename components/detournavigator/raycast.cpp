#include "raycast.hpp"

#include <components/misc/vsgmath.hpp>

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <array>

namespace DetourNavigator
{
    std::optional<vsg::vec3> raycast(const dtNavMeshQuery& navMeshQuery, const vsg::vec3& halfExtents,
        const vsg::vec3& start, const vsg::vec3& end, const Flags includeFlags)
    {
        dtQueryFilter queryFilter;
        queryFilter.setIncludeFlags(includeFlags);

        dtPolyRef ref = 0;
        if (dtStatus status
            = navMeshQuery.findNearestPoly(start.data(), halfExtents.data(), &queryFilter, &ref, nullptr);
            dtStatusFailed(status) || ref == 0)
            return {};

        const unsigned options = 0;
        std::array<dtPolyRef, 16> path;
        dtRaycastHit hit;
        hit.path = path.data();
        hit.maxPath = static_cast<int>(path.size());
        if (dtStatus status = navMeshQuery.raycast(ref, start.data(), end.data(), &queryFilter, options, &hit);
            dtStatusFailed(status) || hit.pathCount == 0)
            return {};

        vsg::vec3 hitPosition;
        if (dtStatus status
            = navMeshQuery.closestPointOnPoly(path[hit.pathCount - 1], end.data(), hitPosition.data(), nullptr);
            dtStatusFailed(status))
            return {};

        return hitPosition;
    }
}
