// Task 0140: starting a neutral session at a spot of a world's exterior (`--start @<world>,<x>,<y>[,<z>]`).
// The pure parts: the start text, the bilinear ground height of a cell, and the land lookup through a pack's terrain
// source (parent worlds, default height, every way there is no land). Written from the task spec before the
// implementation; the first run was against a stub.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <tuple>

#include "apps/openmw/mwworld/neutralstart.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::NeutralStartTarget;
    using Kind = MWWorld::NeutralStartTarget::Kind;

    TEST(NeutralStartParse, a_plain_name_is_an_interior)
    {
        const NeutralStartTarget t = MWWorld::parseNeutralStart("WhiterunBanneredMare");
        EXPECT_EQ(t.mKind, Kind::Interior);
        EXPECT_EQ(t.mCell, "WhiterunBanneredMare");
    }

    TEST(NeutralStartParse, an_empty_text_stays_an_interior_with_no_name)
    {
        const NeutralStartTarget t = MWWorld::parseNeutralStart("");
        EXPECT_EQ(t.mKind, Kind::Interior);
        EXPECT_TRUE(t.mCell.empty());
    }

    TEST(NeutralStartParse, a_world_and_two_numbers_are_an_exterior_without_z)
    {
        const NeutralStartTarget t = MWWorld::parseNeutralStart("@Tamriel,1000,-2000.5");
        ASSERT_EQ(t.mKind, Kind::Exterior) << t.mError;
        EXPECT_EQ(t.mWorld, "Tamriel");
        EXPECT_DOUBLE_EQ(t.mX, 1000.0);
        EXPECT_DOUBLE_EQ(t.mY, -2000.5);
        EXPECT_FALSE(t.mZ.has_value());
    }

    TEST(NeutralStartParse, a_third_number_is_z_and_spaces_are_ignored)
    {
        const NeutralStartTarget t = MWWorld::parseNeutralStart("@ Tamriel , 1.5 , -2 , 300.25 ");
        ASSERT_EQ(t.mKind, Kind::Exterior) << t.mError;
        EXPECT_EQ(t.mWorld, "Tamriel");
        EXPECT_DOUBLE_EQ(t.mX, 1.5);
        EXPECT_DOUBLE_EQ(t.mY, -2.0);
        ASSERT_TRUE(t.mZ.has_value());
        EXPECT_DOUBLE_EQ(*t.mZ, 300.25);
    }

    TEST(NeutralStartParse, malformed_exterior_texts_are_refused_with_a_message)
    {
        for (const char* text : { "@", "@Tamriel", "@Tamriel,1", "@,1,2", "@Tamriel,a,2", "@Tamriel,1,2,3,4",
                 "@Tamriel,nan,2", "@Tamriel,1,inf", "@Tamriel,1e9,2", "@Tamriel,1,-1e9", "@Tamriel,1,2,",
                 "@Tamriel,1x,2", "@Tamriel,,2", "@Tamriel,1,2,nan" })
        {
            const NeutralStartTarget t = MWWorld::parseNeutralStart(text);
            EXPECT_EQ(t.mKind, Kind::Invalid) << text;
            EXPECT_FALSE(t.mError.empty()) << text;
        }
    }

    // A 3 x 3 vertex cell of 4096 units: spacing 2048, h = col * 100 + row * 300 at the vertices.
    TerrainCell planeCell(std::int32_t gx, std::int32_t gy)
    {
        TerrainCell c;
        c.gridX = gx;
        c.gridY = gy;
        c.vertsPerSide = 3;
        c.hasHeights = true;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                c.heights.push_back(static_cast<float>(col * 100 + row * 300));
        return c;
    }

    TEST(NeutralStartHeight, interpolates_between_vertices_in_a_negative_cell)
    {
        // cell (1, -1): local (1024, 3072) is u = 0.5, v = 1.5: 50 + 450
        const auto h = MWWorld::neutralCellHeightAt(planeCell(1, -1), 4096.0, 4096.0 + 1024.0, -4096.0 + 3072.0);
        ASSERT_TRUE(h.has_value());
        EXPECT_NEAR(*h, 500.0, 1e-4);
    }

    TEST(NeutralStartHeight, a_vertex_is_exact_and_the_far_edge_clamps)
    {
        const TerrainCell c = planeCell(0, 0);
        EXPECT_NEAR(*MWWorld::neutralCellHeightAt(c, 4096.0, 2048.0, 2048.0), 400.0, 1e-4);
        EXPECT_NEAR(*MWWorld::neutralCellHeightAt(c, 4096.0, 4096.0, 4096.0), 800.0, 1e-4);
        EXPECT_NEAR(*MWWorld::neutralCellHeightAt(c, 4096.0, 9999.0, -9999.0), 200.0, 1e-4);
    }

    TEST(NeutralStartHeight, no_heights_or_a_wrong_size_or_nan_give_nothing)
    {
        TerrainCell c = planeCell(0, 0);
        c.hasHeights = false;
        EXPECT_FALSE(MWWorld::neutralCellHeightAt(c, 4096.0, 1.0, 1.0).has_value());
        c = planeCell(0, 0);
        c.heights.pop_back();
        EXPECT_FALSE(MWWorld::neutralCellHeightAt(c, 4096.0, 1.0, 1.0).has_value());
        c = planeCell(0, 0);
        c.heights[4] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(MWWorld::neutralCellHeightAt(c, 4096.0, 2048.0, 2048.0).has_value());
        EXPECT_FALSE(MWWorld::neutralCellHeightAt(planeCell(0, 0), 4096.0, std::nan(""), 1.0).has_value());
    }

    class FakeLand : public WorldCellSource
    {
    public:
        std::map<WorldId, WorldSpaceInfo> mWorlds;
        std::map<std::tuple<WorldId, int, int>, TerrainCell> mLand;
        std::map<std::tuple<WorldId, int, int>, TerrainResult> mResults; // overrides: NoLand, Failed, NotSupported
        int mCalls = 0;

        std::string_view gameId() const override { return "fake"; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
        bool buildCell(WorldId, WorldCell&, std::string& error) override
        {
            error = "no";
            return false;
        }
        bool worldInfo(WorldId world, WorldSpaceInfo& out) const override
        {
            const auto it = mWorlds.find(world);
            if (it == mWorlds.end())
                return false;
            out = it->second;
            return true;
        }
        TerrainResult buildTerrain(
            WorldId world, std::int32_t x, std::int32_t y, TerrainCell& out, std::string& error) override
        {
            ++mCalls;
            const auto key = std::make_tuple(world, static_cast<int>(x), static_cast<int>(y));
            if (const auto r = mResults.find(key); r != mResults.end())
            {
                if (r->second == TerrainResult::Failed)
                    error = "boom in cell";
                return r->second;
            }
            const auto it = mLand.find(key);
            if (it == mLand.end())
                return TerrainResult::NoLand;
            out = it->second;
            return TerrainResult::Built;
        }
    };

    WorldSpaceInfo info(WorldId id, WorldId parent = kNoWorldId, std::uint32_t usage = 0)
    {
        WorldSpaceInfo w;
        w.id = id;
        w.editorId = "W" + std::to_string(id);
        w.parent = parent;
        w.parentUsage = usage;
        w.cellSizeUnits = 4096.0f;
        w.vertsPerSide = 3;
        w.defaultLandHeight = 7.0f;
        return w;
    }

    TEST(NeutralStartLand, reads_the_height_under_the_spot)
    {
        FakeLand s;
        s.mWorlds[1] = info(1);
        s.mLand[{ 1, -1, 0 }] = planeCell(-1, 0);
        std::string error;
        // x = -4096 + 2048, y = 2048: vertex (1, 1) = 400
        const auto h = MWWorld::neutralLandHeight(s, 1, -2048.0, 2048.0, error);
        ASSERT_TRUE(h.has_value()) << error;
        EXPECT_NEAR(*h, 400.0, 1e-4);
    }

    TEST(NeutralStartLand, a_cell_with_land_but_no_heights_uses_the_default_height)
    {
        FakeLand s;
        s.mWorlds[1] = info(1);
        TerrainCell c = planeCell(0, 0);
        c.hasHeights = false;
        c.heights.clear();
        s.mLand[{ 1, 0, 0 }] = c;
        std::string error;
        const auto h = MWWorld::neutralLandHeight(s, 1, 10.0, 10.0, error);
        ASSERT_TRUE(h.has_value()) << error;
        EXPECT_NEAR(*h, 7.0, 1e-6);
    }

    TEST(NeutralStartLand, no_land_without_a_parent_that_lends_it_is_refused)
    {
        FakeLand s;
        s.mWorlds[1] = info(1);
        std::string error;
        EXPECT_FALSE(MWWorld::neutralLandHeight(s, 1, 10.0, 10.0, error).has_value());
        EXPECT_FALSE(error.empty());
    }

    TEST(NeutralStartLand, a_parent_that_lends_its_land_is_used_and_one_that_does_not_is_not)
    {
        FakeLand s;
        s.mWorlds[1] = info(1);
        s.mWorlds[2] = info(2, 1, TerrainParentUsage::Land);
        s.mWorlds[3] = info(3, 1, 0);
        s.mLand[{ 1, 0, 0 }] = planeCell(0, 0);
        std::string error;
        const auto borrowed = MWWorld::neutralLandHeight(s, 2, 2048.0, 2048.0, error);
        ASSERT_TRUE(borrowed.has_value()) << error;
        EXPECT_NEAR(*borrowed, 400.0, 1e-4);
        error.clear();
        EXPECT_FALSE(MWWorld::neutralLandHeight(s, 3, 2048.0, 2048.0, error).has_value());
        EXPECT_FALSE(error.empty());
    }

    TEST(NeutralStartLand, a_parent_cycle_ends_with_an_error)
    {
        FakeLand s;
        s.mWorlds[1] = info(1, 2, TerrainParentUsage::Land);
        s.mWorlds[2] = info(2, 1, TerrainParentUsage::Land);
        std::string error;
        EXPECT_FALSE(MWWorld::neutralLandHeight(s, 1, 10.0, 10.0, error).has_value());
        EXPECT_FALSE(error.empty());
        EXPECT_LT(s.mCalls, 20);
    }

    TEST(NeutralStartLand, unknown_world_failed_build_and_unsupported_terrain_are_refused_with_a_reason)
    {
        FakeLand s;
        s.mWorlds[1] = info(1);
        std::string error;
        EXPECT_FALSE(MWWorld::neutralLandHeight(s, 9, 1.0, 1.0, error).has_value());
        EXPECT_FALSE(error.empty());

        s.mResults[{ 1, 0, 0 }] = TerrainResult::Failed;
        error.clear();
        EXPECT_FALSE(MWWorld::neutralLandHeight(s, 1, 1.0, 1.0, error).has_value());
        EXPECT_NE(error.find("boom in cell"), std::string::npos) << error;

        s.mResults[{ 1, 0, 0 }] = TerrainResult::NotSupported;
        error.clear();
        EXPECT_FALSE(MWWorld::neutralLandHeight(s, 1, 1.0, 1.0, error).has_value());
        EXPECT_FALSE(error.empty());
    }

    TEST(NeutralStartLand, non_finite_positions_are_refused)
    {
        FakeLand s;
        s.mWorlds[1] = info(1);
        std::string error;
        EXPECT_FALSE(MWWorld::neutralLandHeight(s, 1, std::nan(""), 1.0, error).has_value());
        EXPECT_FALSE(error.empty());
    }
}
