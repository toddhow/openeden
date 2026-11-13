// Task 0153: the pure parts of the neutral grass: a pack's grass as groundcover instances, and the squares in range for
// cells of another size. No renderer.
#include "apps/openmw/mwrender/groundcoverinstances.hpp"
#include "apps/openmw/mwrender/neutralgrass.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{
    namespace sdk = openeden::sdk::game;
    using Cell = std::pair<int, int>;

    bool contains(const std::vector<Cell>& cells, int x, int y)
    {
        return std::find(cells.begin(), cells.end(), Cell(x, y)) != cells.end();
    }

    sdk::GrassInstance tuft(std::uint32_t model, float x, float y, float z, float yaw, float scale)
    {
        sdk::GrassInstance t;
        t.model = model;
        t.x = x;
        t.y = y;
        t.z = z;
        t.yaw = yaw;
        t.scale = scale;
        return t;
    }

    TEST(NeutralGrassCells, aSmallerCellSizeGivesTheCellsWithinReach)
    {
        // A camera in the middle of the square (0, 0) of 4096 units, 6144 units of reach.
        const std::vector<Cell> cells = MWRender::groundcoverCellsInRange(2048.f, 2048.f, 6144.f, 4096.f);
        EXPECT_EQ(cells.size(), 13u);
        EXPECT_TRUE(contains(cells, 0, 0));
        EXPECT_TRUE(contains(cells, 1, 1));
        EXPECT_TRUE(contains(cells, -1, -1));
        EXPECT_TRUE(contains(cells, 2, 0));
        EXPECT_TRUE(contains(cells, -2, 0));
        EXPECT_TRUE(contains(cells, 0, 2));
        EXPECT_TRUE(contains(cells, 0, -2));
        EXPECT_FALSE(contains(cells, 2, 1));
        EXPECT_FALSE(contains(cells, 2, 2));
    }

    TEST(NeutralGrassCells, theDefaultCellSizeIsMorrowindsAndTheResultIsSorted)
    {
        const std::vector<Cell> a = MWRender::groundcoverCellsInRange(1000.f, -3000.f, 6144.f);
        const std::vector<Cell> b = MWRender::groundcoverCellsInRange(1000.f, -3000.f, 6144.f, 8192.f);
        EXPECT_EQ(a, b);
        EXPECT_TRUE(std::is_sorted(a.begin(), a.end()));
    }

    TEST(NeutralGrassCells, zeroReachIsTheCameraSquare)
    {
        const std::vector<Cell> cells = MWRender::groundcoverCellsInRange(-100.f, 5000.f, 0.f, 4096.f);
        ASSERT_EQ(cells.size(), 1u);
        EXPECT_EQ(cells[0], Cell(-1, 1));
    }

    TEST(NeutralGrassInstances, tuftsAreGroupedByModelWithYawAndScale)
    {
        sdk::GrassCell cell;
        cell.models = { { "meshes\\landscape\\grass\\a.nif", false }, { "meshes\\landscape\\grass\\b.nif", false } };
        cell.instances = { tuft(0, 10.f, 20.f, 30.f, 1.5f, 0.9f), tuft(1, 40.f, 50.f, 60.f, 0.25f, 1.1f),
            tuft(0, 70.f, 80.f, 90.f, 3.0f, 1.0f) };
        const MWRender::GroundcoverInstances out = MWRender::groundcoverFromGrass(cell);
        ASSERT_EQ(out.size(), 2u);
        const auto a = out.find(VFS::Path::NormalizedView("meshes/landscape/grass/a.nif"));
        const auto b = out.find(VFS::Path::NormalizedView("meshes/landscape/grass/b.nif"));
        ASSERT_NE(a, out.end());
        ASSERT_NE(b, out.end());
        ASSERT_EQ(a->second.size(), 2u);
        ASSERT_EQ(b->second.size(), 1u);
        EXPECT_FLOAT_EQ(a->second[0].position.pos[0], 10.f);
        EXPECT_FLOAT_EQ(a->second[0].position.pos[1], 20.f);
        EXPECT_FLOAT_EQ(a->second[0].position.pos[2], 30.f);
        EXPECT_FLOAT_EQ(a->second[0].position.rot[0], 0.f);
        EXPECT_FLOAT_EQ(a->second[0].position.rot[1], 0.f);
        EXPECT_FLOAT_EQ(a->second[0].position.rot[2], 1.5f);
        EXPECT_FLOAT_EQ(a->second[0].scale, 0.9f);
        EXPECT_FLOAT_EQ(a->second[1].position.pos[0], 70.f); // the pack's order is kept
        EXPECT_FLOAT_EQ(b->second[0].scale, 1.1f);
    }

    TEST(NeutralGrassInstances, aBadModelIndexOrANonFiniteValueIsDropped)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        sdk::GrassCell cell;
        cell.models = { { "meshes\\a.nif", false } };
        cell.instances = { tuft(1, 1.f, 1.f, 1.f, 0.f, 1.f), tuft(0, nan, 1.f, 1.f, 0.f, 1.f),
            tuft(0, 1.f, 1.f, 1.f, 0.f, std::numeric_limits<float>::infinity()), tuft(0, 5.f, 6.f, 7.f, 0.f, 1.f) };
        const MWRender::GroundcoverInstances out = MWRender::groundcoverFromGrass(cell);
        ASSERT_EQ(out.size(), 1u);
        ASSERT_EQ(out.begin()->second.size(), 1u);
        EXPECT_FLOAT_EQ(out.begin()->second[0].position.pos[0], 5.f);
    }

    TEST(NeutralGrassInstances, anEmptyCellGivesNothing)
    {
        EXPECT_TRUE(MWRender::groundcoverFromGrass(sdk::GrassCell()).empty());
        sdk::GrassCell onlyModels;
        onlyModels.models = { { "meshes\\a.nif", false } };
        EXPECT_TRUE(MWRender::groundcoverFromGrass(onlyModels).empty());
    }
}
