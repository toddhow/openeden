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
