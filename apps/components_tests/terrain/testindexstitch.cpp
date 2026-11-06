#include <gtest/gtest.h>

#include <components/terrain/indexbuffer.hpp>

namespace
{
    using Terrain::CreateIndexBuffer;
    using Terrain::maxStitchDelta;
    using Terrain::safeLodFlags;

    // Task 0090: a chunk next to a much coarser LOD tile asked for a stitch step larger than its grid can take
    // (`[WorldLod] terrain tile threshold = 100000` drew one tile over the whole world) and the index fill wrote
    // past its buffer, which corrupted VSG's allocator. The step is now cut to what the grid can take.

    TEST(TerrainIndexStitch, the_limit_is_the_largest_power_of_two_dividing_the_quad_count)
    {
        EXPECT_EQ(maxStitchDelta(65), 6u);
        EXPECT_EQ(maxStitchDelta(129), 7u);
        EXPECT_EQ(maxStitchDelta(33), 5u);
        EXPECT_EQ(maxStitchDelta(9), 3u);
        EXPECT_EQ(maxStitchDelta(49), 4u);
        EXPECT_EQ(maxStitchDelta(7), 1u);
        EXPECT_EQ(maxStitchDelta(5), 2u);
        EXPECT_EQ(maxStitchDelta(4), 0u);
    }

    TEST(TerrainIndexStitch, a_grid_too_small_to_stitch_has_no_limit_to_use)
    {
        EXPECT_EQ(maxStitchDelta(0), 0u);
        EXPECT_EQ(maxStitchDelta(1), 0u);
        EXPECT_EQ(maxStitchDelta(2), 0u);
    }

    TEST(TerrainIndexStitch, each_edge_is_cut_to_the_limit_on_its_own)
    {
        // north 15, east 2, south 9, west 1 on a 65-vertex grid (limit 6)
        const uint32_t flags = 15u | (2u << 4) | (9u << 8) | (1u << 12);
        EXPECT_EQ(safeLodFlags(65, flags), 6u | (2u << 4) | (6u << 8) | (1u << 12));
    }

    TEST(TerrainIndexStitch, flags_within_the_limit_are_kept)
    {
        const uint32_t flags = 3u | (1u << 4) | (0u << 8) | (6u << 12);
        EXPECT_EQ(safeLodFlags(65, flags), flags);
    }

    TEST(TerrainIndexStitch, bits_above_the_fourth_step_are_dropped)
    {
        EXPECT_EQ(safeLodFlags(65, 0xabcd0001u), 1u);
        EXPECT_EQ(safeLodFlags(65, 0xffff0000u), 0u);
    }

    // The fill must write exactly the indices it counted, for every grid size and every step the key can carry. The
    // buffer's own check throws when the count is off (after the fact: a count that is too high has already
    // overwritten memory, which is the fault this task fixed).
    TEST(TerrainIndexStitch, every_key_builds_a_buffer_that_is_exactly_full)
    {
        CreateIndexBuffer create;
        for (uint32_t verts = 2; verts <= 70; ++verts)
        {
            for (uint32_t delta = 0; delta <= 15; ++delta)
            {
                const uint32_t flags = delta | (delta << 4) | (delta << 8) | (delta << 12);
                EXPECT_NO_THROW(create.create({ verts, flags })) << verts << " vertices, step " << delta;
            }
        }
    }

    TEST(TerrainIndexStitch, mixed_steps_on_the_four_edges_fill_exactly)
    {
        CreateIndexBuffer create;
        for (const uint32_t verts : { 9u, 17u, 33u, 49u, 65u })
        {
            const uint32_t limit = maxStitchDelta(verts);
            for (uint32_t n = 0; n <= limit; ++n)
            {
                for (uint32_t e = 0; e <= limit; ++e)
                {
                    for (uint32_t s = 0; s <= limit; ++s)
                    {
                        for (uint32_t w = 0; w <= limit; ++w)
                        {
                            EXPECT_NO_THROW(create.create({ verts, n | (e << 4) | (s << 8) | (w << 12) }));
                        }
                    }
                }
            }
        }
    }

    TEST(TerrainIndexStitch, a_very_large_grid_is_cut_to_the_four_bit_field)
    {
        EXPECT_EQ(maxStitchDelta(65537), 16u);
        EXPECT_EQ(safeLodFlags(65537, 0xffffu), 0xffffu);
    }
}
