// Task 0087 (stage a): the text of a terrain tile swap in the log and the load trace.
#include <gtest/gtest.h>

#include <components/lod/tileswap.hpp>

#include <cstring>
#include <string>

namespace
{
    using namespace Lod;

    TEST(LodTileSwap, kinds_have_the_documented_words)
    {
        EXPECT_STREQ(tileSwapKindName(TileSwapKind::New), "new");
        EXPECT_STREQ(tileSwapKindName(TileSwapKind::Refined), "refined");
        EXPECT_STREQ(tileSwapKindName(TileSwapKind::Coarsened), "coarsened");
        EXPECT_STREQ(tileSwapKindName(TileSwapKind::ToLive), "to live");
        EXPECT_STREQ(tileSwapKindName(TileSwapKind::Removed), "removed");
    }

    TEST(LodTileSwap, trace_names_are_distinct_and_prefixed)
    {
        const TileSwapKind kinds[] = { TileSwapKind::New, TileSwapKind::Refined, TileSwapKind::Coarsened,
            TileSwapKind::ToLive, TileSwapKind::Removed };
        for (const TileSwapKind a : kinds)
        {
            EXPECT_EQ(std::strncmp(tileSwapTraceName(a), "lod.tile.", 9), 0);
            for (const TileSwapKind b : kinds)
            {
                if (a != b)
                {
                    EXPECT_STRNE(tileSwapTraceName(a), tileSwapTraceName(b));
                }
            }
        }
        EXPECT_STREQ(tileSwapTraceName(TileSwapKind::ToLive), "lod.tile.tolive");
    }

    TEST(LodTileSwap, line_names_frame_tile_cell_and_kind)
    {
        const auto id = tileContainingCell(0, 3, -24, 40);
        ASSERT_TRUE(id.has_value());
        const std::string line = formatTileSwap(1234, *id, TileSwapKind::Refined);
        EXPECT_EQ(line.rfind("World LOD tile swap: frame 1234, level 3 tile (", 0), 0u);
        EXPECT_NE(line.find(") at cell (-24, 40): refined"), std::string::npos);
        EXPECT_EQ(line.find('\n'), std::string::npos);
    }

    TEST(LodTileSwap, line_is_stable_for_the_same_input)
    {
        const auto id = tileContainingCell(0, 1, 5, -7);
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(formatTileSwap(7, *id, TileSwapKind::ToLive), formatTileSwap(7, *id, TileSwapKind::ToLive));
        EXPECT_NE(formatTileSwap(7, *id, TileSwapKind::ToLive), formatTileSwap(8, *id, TileSwapKind::ToLive));
        EXPECT_NE(formatTileSwap(7, *id, TileSwapKind::ToLive), formatTileSwap(7, *id, TileSwapKind::New));
    }
}
