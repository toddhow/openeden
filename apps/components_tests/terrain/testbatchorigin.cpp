// Task 0125 (stage E2 follow-up): the terrain vertex shader assumes 8192 unit cells; the batch origin of a storage with
// other cells (Skyrim SE: 4096) has to be rescaled, or the terrain is drawn at the wrong place. Written before the
// code.
#include <components/terrain/batchdata.hpp>

#include <gtest/gtest.h>

namespace
{
    // What terrain/main.vert does with the origin (cameraPos in units).
    vsg::vec2 shaderOffset(const vsg::vec2& uniformOrigin, const vsg::vec2& cameraPos)
    {
        const float cellWorldSize = Terrain::shaderCellWorldSize;
        return (uniformOrigin - cameraPos / cellWorldSize) * cellWorldSize;
    }

    TEST(TerrainBatchOrigin, morrowinds_cells_keep_the_origin_bit_for_bit)
    {
        const vsg::vec2 origin(-17.f, 23.f);
        const vsg::vec2 shaded = Terrain::shaderBatchOrigin(origin, 8192.f);
        EXPECT_EQ(shaded.x, origin.x);
        EXPECT_EQ(shaded.y, origin.y);
    }

    TEST(TerrainBatchOrigin, a_4096_unit_cell_is_rescaled_to_half)
    {
        const vsg::vec2 shaded = Terrain::shaderBatchOrigin(vsg::vec2(6.f, -2.f), 4096.f);
        EXPECT_FLOAT_EQ(shaded.x, 3.f);
        EXPECT_FLOAT_EQ(shaded.y, -1.f);
    }

    TEST(TerrainBatchOrigin, the_shader_then_puts_the_batch_at_its_world_offset_from_the_camera)
    {
        for (float cell : { 4096.f, 8192.f, 2048.f })
        {
            const vsg::vec2 origin(6.f, -2.f);
            const vsg::vec2 camera(25670.f, -7633.f);
            const vsg::vec2 offset = shaderOffset(Terrain::shaderBatchOrigin(origin, cell), camera);
            EXPECT_NEAR(offset.x, origin.x * cell - camera.x, 0.01f) << cell;
            EXPECT_NEAR(offset.y, origin.y * cell - camera.y, 0.01f) << cell;
        }
    }

    TEST(TerrainBatchOrigin, a_bad_cell_size_leaves_the_origin_alone)
    {
        const vsg::vec2 origin(1.f, 2.f);
        EXPECT_EQ(Terrain::shaderBatchOrigin(origin, 0.f).x, 1.f);
        EXPECT_EQ(Terrain::shaderBatchOrigin(origin, -4096.f).y, 2.f);
    }
}
