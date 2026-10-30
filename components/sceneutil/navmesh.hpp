#ifndef OPENMW_COMPONENTS_SCENEUTIL_NAVMESH_H
#define OPENMW_COMPONENTS_SCENEUTIL_NAVMESH_H

class dtNavMesh;
struct dtMeshTile;

namespace DetourNavigator
{
    struct Settings;
}

namespace SceneUtil
{
    class DebugPrimitiveSink;

    enum NavMeshTileDrawFlags : unsigned char
    {
        NavMeshTileDrawFlagsOffMeshConnections = 1,
        NavMeshTileDrawFlagsClosedList = 1 << 1,
        NavMeshTileDrawFlagsColorTiles = 1 << 2,
        NavMeshTileDrawFlagsHeat = 1 << 3,
    };

    /// Draws one navmesh tile into sink in world coordinates; returns false when the tile has no header.
    bool drawNavMeshTile(DebugPrimitiveSink& sink, const dtNavMesh& navMesh, const dtMeshTile& meshTile,
        const DetourNavigator::Settings& settings, unsigned char flags, unsigned minSalt, unsigned maxSalt);
}

#endif
