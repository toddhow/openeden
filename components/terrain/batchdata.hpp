#ifndef OPENMW_COMPONENTS_TERRAIN_BATCHDATA_H
#define OPENMW_COMPONENTS_TERRAIN_BATCHDATA_H

#include <vsg/maths/vec2.h>

namespace Pipeline::Data
{
    using vsg::vec2;
#include <files/shaders/terrain/data.glsl>
}

namespace Terrain
{
    /// The cell size the terrain vertex shaders assume (`cellWorldSize` in terrain/main.vert and terrain.vert.slang):
    /// they turn the batch origin, in cells, into units with it and the camera position into cells with it.
    /// Morrowind's.
    inline constexpr float shaderCellWorldSize = 8192.f;

    /// The origin to put in the batch uniform for a storage whose cells are `cellWorldSize` units: the shader computes
    /// (origin - camera / 8192) * 8192, which is only right for 8192 unit cells, so the origin is rescaled to what
    /// it would be in cells of 8192 (task 0125: Skyrim's 4096 unit cells were drawn twice as far out, and nothing
    /// showed). Exactly the origin for 8192 (Morrowind: the same bits as before).
    inline vsg::vec2 shaderBatchOrigin(const vsg::vec2& originInCells, float cellWorldSize)
    {
        if (!(cellWorldSize > 0.f) || cellWorldSize == shaderCellWorldSize)
            return originInCells;
        return originInCells * (cellWorldSize / shaderCellWorldSize);
    }

    /// The uniform of one terrain batch (binding 2 of the terrain's texture set): where it is, how many vertices it has
    /// and how far it is scaled. The fade is 1 (opaque) until a world LOD tile is made to dither (task 0087).
    inline Pipeline::Data::Batch makeBatchUniform(const vsg::vec2& origin, float numVerts, float vertexScale)
    {
        Pipeline::Data::Batch batch{};
        batch.origin = origin;
        batch.numVerts = numVerts;
        batch.vertexScale = vertexScale;
        batch.fade = 1.f;
        return batch;
    }
}

#endif
