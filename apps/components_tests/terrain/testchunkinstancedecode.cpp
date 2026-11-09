// A live terrain chunk's fade rides in the high nibbles of firstInstance (task 0088, Terrain::encodeChunkShare); the
// CPU copy of the vertex shader that intersectors use (Terrain::HeightArrayState::vertexArray) must read lod, shift and
// grid position the way the shader does, nibble by nibble. It read whole bytes: a fading chunk then gave a wild lod and
// shift, a proxy vertex array smaller than the loops wrote, and the game crashed in VSG's allocator a moment after
// start (the heap was overwritten by floats of the proxy vertices; the crash dump pointed at an unrelated allocation).

#include <components/terrain/chunkfade.hpp>
#include <components/terrain/heightarraystate.hpp>

#include <gtest/gtest.h>

#include <vsg/nodes/Group.h>

#include <cstdint>
#include <vector>

namespace
{
    std::uint32_t pack(std::uint32_t lod, std::uint32_t shift, std::uint32_t x, std::uint32_t y)
    {
        return lod | (shift << 8) | (x << 16) | (y << 24);
    }

    std::vector<vsg::vec3> proxyOf(Terrain::HeightArrayState& state, std::uint32_t instance)
    {
        auto array = state.vertexArray(instance);
        std::vector<vsg::vec3> out;
        if (array)
            out.assign(array->begin(), array->end());
        return out;
    }
}

TEST(TerrainChunkInstanceDecode, plain_instances_decode_to_their_fields)
{
    const Terrain::ChunkGeometry g = Terrain::decodeChunkGeometry(pack(3, 5, 9, 200));
    EXPECT_EQ(g.lod, 3u);
    EXPECT_EQ(g.shift, 5u);
    EXPECT_EQ(g.gridX, 9u);
    EXPECT_EQ(g.gridY, 200u);
}

TEST(TerrainChunkInstanceDecode, the_fade_never_changes_the_geometry_fields)
{
    for (std::uint32_t lod = 0; lod < 8; ++lod)
        for (std::uint32_t shift = 0; shift < 8; ++shift)
            for (std::uint32_t x = 0; x < 16; x += 5)
            {
                const std::uint32_t base = pack(lod, shift, x, 7);
                for (float fade = 0.f; fade <= 1.f; fade += 0.05f)
                {
                    for (bool inverted : { false, true })
                    {
                        const std::uint32_t instance
                            = Terrain::encodeChunkShare(base, Terrain::ChunkShare{ fade, inverted });
                        const Terrain::ChunkGeometry g = Terrain::decodeChunkGeometry(instance);
                        ASSERT_EQ(g.lod, lod) << "fade " << fade << " inverted " << inverted;
                        ASSERT_EQ(g.shift, shift) << "fade " << fade << " inverted " << inverted;
                        ASSERT_EQ(g.gridX, x) << "fade " << fade << " inverted " << inverted;
                        ASSERT_EQ(g.gridY, 7u);
                    }
                }
            }
}

TEST(TerrainChunkInstanceDecode, the_cpu_copy_of_the_vertex_stage_ignores_the_fade)
{
    auto heights = vsg::floatArray2D::create(65, 65);
    for (uint32_t y = 0; y < 65; ++y)
        for (uint32_t x = 0; x < 65; ++x)
            heights->set(x, y, 0.5f * x + 0.25f * y);

    auto parent = vsg::Group::create();
    Terrain::HeightArrayState state;
    state.parent = parent.get();
    state.vertexScale = 8192.f;
    state.origin = vsg::vec2(0.f, 0.f);
    state.apply(*heights);

    const std::uint32_t base = pack(1, 2, 1, 3);
    const std::vector<vsg::vec3> plain = proxyOf(state, base);
    ASSERT_EQ(plain.size(), 9u * 9u);

    for (float fade : { 0.f, 0.2f, 0.3f, 0.5f, 0.77f, 0.99f })
    {
        for (bool inverted : { false, true })
        {
            const std::uint32_t instance = Terrain::encodeChunkShare(base, Terrain::ChunkShare{ fade, inverted });
            ASSERT_NE(instance, base) << "the test needs an instance that carries a fade (fade " << fade << ")";
            const std::vector<vsg::vec3> faded = proxyOf(state, instance);
            ASSERT_EQ(faded.size(), plain.size()) << "fade " << fade << " inverted " << inverted;
            for (size_t i = 0; i < plain.size(); ++i)
                ASSERT_EQ(faded[i], plain[i]) << "vertex " << i << " fade " << fade << " inverted " << inverted;
        }
    }
}

// A batch is up to 16 cells (Paging::mMaxBatchSize) and its finest chunk is Bounds::minSize (1/8 cell) wide, so a
// chunk's grid x reaches 127 (7 bits, bits 16 to 22; bit 23 is the "inverted" flag). The task 0088 decode read four
// bits and drew every finest chunk with x of 16 or more in the wrong place: a hole next to the player that moved with
// him.
TEST(TerrainChunkInstanceDecode, a_grid_x_up_to_127_survives_every_fade_and_the_inverted_flag)
{
    for (std::uint32_t x : { 0u, 15u, 16u, 17u, 31u, 32u, 64u, 100u, 127u })
    {
        const std::uint32_t base = pack(2, 4, x, 90);
        const Terrain::ChunkGeometry plain = Terrain::decodeChunkGeometry(base);
        ASSERT_EQ(plain.gridX, x);
        for (float fade : { 0.f, 0.3f, 0.77f, 1.f })
        {
            for (bool inverted : { false, true })
            {
                const std::uint32_t instance = Terrain::encodeChunkShare(base, Terrain::ChunkShare{ fade, inverted });
                const Terrain::ChunkGeometry g = Terrain::decodeChunkGeometry(instance);
                ASSERT_EQ(g.gridX, x) << "fade " << fade << " inverted " << inverted;
                ASSERT_EQ(g.gridY, 90u);
            }
        }
    }
}
