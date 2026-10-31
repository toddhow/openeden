#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_MAKENAVMESH_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_MAKENAVMESH_H

#include "recastmesh.hpp"
#include "tileposition.hpp"

#include <components/esm/refid.hpp>
#include <components/misc/vsgmath.hpp>

#include <memory>
#include <vector>

class dtNavMesh;
struct rcConfig;

namespace DetourNavigator
{
    struct Settings;
    struct PreparedNavMeshData;
    struct NavMeshData;
    struct OffMeshConnection;
    struct AgentBounds;
    struct RecastSettings;

    inline float getLength(const vsg::ivec2& value)
    {
        return std::sqrt(float(value.x * value.x + value.y * value.y));
    }

    inline float getDistance(const TilePosition& lhs, const TilePosition& rhs)
    {
        return getLength(lhs - rhs);
    }

    inline bool shouldAddTile(const TilePosition& changedTile, const TilePosition& playerTile, int maxTiles)
    {
        const float distance = getDistance(changedTile, playerTile);
        const auto expectedTilesCount = std::ceil(Misc::VsgMath::PI * (distance * distance));
        return expectedTilesCount <= maxTiles;
    }

    inline bool isEmpty(const RecastMesh& recastMesh)
    {
        return recastMesh.getMesh().getIndices().empty() && recastMesh.getWater().empty()
            && recastMesh.getHeightfields().empty() && recastMesh.getFlatHeightfields().empty();
    }

    std::unique_ptr<PreparedNavMeshData> prepareNavMeshTileData(const RecastMesh& recastMesh, ESM::RefId worldspace,
        const TilePosition& tilePosition, const AgentBounds& agentBounds, const RecastSettings& settings);

    NavMeshData makeNavMeshTileData(const PreparedNavMeshData& data,
        const std::vector<OffMeshConnection>& offMeshConnections, const AgentBounds& agentBounds,
        const TilePosition& tile, const RecastSettings& settings);

    void initEmptyNavMesh(const Settings& settings, dtNavMesh& navMesh);

    bool isSupportedAgentBounds(const RecastSettings& settings, const AgentBounds& agentBounds);
}

#endif
