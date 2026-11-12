// Task 0129: the cell water set (components/render/watercells.hpp).
#include <components/render/watercells.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
    using Render::WaterCells;
    constexpr float kSize = 4096.f;
}

TEST(RenderWaterCells, anEmptySetHasNoWater)
{
    WaterCells cells;
    EXPECT_TRUE(cells.empty());
    EXPECT_FALSE(cells.isUnderwater(10.f, 10.f, -1e6f));
    EXPECT_EQ(cells.find(0, 0), nullptr);
}

TEST(RenderWaterCells, aSurfaceIsFoundByItsGridSquare)
{
    WaterCells cells;
    ASSERT_TRUE(cells.set(2, -3, kSize, 500.f));
    ASSERT_EQ(cells.size(), 1u);
    const WaterCells::Cell* c = cells.find(2, -3);
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(c->mHeight, 500.f);
    EXPECT_FLOAT_EQ(c->mSize, kSize);
    EXPECT_EQ(cells.find(2, 3), nullptr);
}

TEST(RenderWaterCells, setAgainReplacesTheHeightAndKeepsOneSurface)
{
    WaterCells cells;
    ASSERT_TRUE(cells.set(1, 1, kSize, 10.f));
    ASSERT_TRUE(cells.set(1, 1, kSize, 20.f));
    EXPECT_EQ(cells.size(), 1u);
    EXPECT_FLOAT_EQ(cells.find(1, 1)->mHeight, 20.f);
}

TEST(RenderWaterCells, removeSaysWhetherThereWasASurface)
{
    WaterCells cells;
    ASSERT_TRUE(cells.set(1, 1, kSize, 10.f));
    EXPECT_FALSE(cells.remove(2, 2));
    EXPECT_TRUE(cells.remove(1, 1));
    EXPECT_TRUE(cells.empty());
    EXPECT_FALSE(cells.remove(1, 1));
}

TEST(RenderWaterCells, badSizesAndHeightsAreRefused)
{
    WaterCells cells;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(cells.set(0, 0, 0.f, 1.f));
    EXPECT_FALSE(cells.set(0, 0, -5.f, 1.f));
    EXPECT_FALSE(cells.set(0, 0, nan, 1.f));
    EXPECT_FALSE(cells.set(0, 0, kSize, nan));
    EXPECT_FALSE(cells.set(0, 0, kSize, inf));
    EXPECT_TRUE(cells.empty());
}

TEST(RenderWaterCells, underwaterNeedsTheSquareAndAPointBelowTheSurface)
{
    WaterCells cells;
    ASSERT_TRUE(cells.set(1, 0, kSize, 100.f)); // x 4096..8192, y 0..4096
    EXPECT_TRUE(cells.isUnderwater(5000.f, 100.f, 99.f));
    EXPECT_FALSE(cells.isUnderwater(5000.f, 100.f, 101.f));
    EXPECT_FALSE(cells.isUnderwater(5000.f, 100.f, 100.f)); // on the surface is not under it
    EXPECT_FALSE(cells.isUnderwater(100.f, 100.f, 0.f)); // the neighbouring square
    EXPECT_FALSE(cells.isUnderwater(5000.f, 4096.f, 0.f)); // the north edge belongs to the next square
    EXPECT_TRUE(cells.isUnderwater(4096.f, 0.f, 0.f)); // the south-west corner belongs to this one
}

TEST(RenderWaterCells, negativeGridSquaresCoverTheirOwnNegativeArea)
{
    WaterCells cells;
    ASSERT_TRUE(cells.set(-1, -1, kSize, 0.f)); // -4096..0 on both axes
    EXPECT_TRUE(cells.isUnderwater(-1.f, -1.f, -5.f));
    EXPECT_TRUE(cells.isUnderwater(-4096.f, -4096.f, -5.f));
    EXPECT_FALSE(cells.isUnderwater(1.f, 1.f, -5.f));
}

TEST(RenderWaterCells, severalSurfacesAtDifferentHeightsAnswerForTheirOwnSquare)
{
    WaterCells cells;
    ASSERT_TRUE(cells.set(0, 0, kSize, 100.f));
    ASSERT_TRUE(cells.set(1, 0, kSize, 900.f));
    EXPECT_FALSE(cells.isUnderwater(10.f, 10.f, 500.f));
    EXPECT_TRUE(cells.isUnderwater(5000.f, 10.f, 500.f));
}
