#include <gtest/gtest.h>

#include <components/terrain/batchindex.hpp>

namespace
{
    using Terrain::batchIndex;

    // Task 0073: the local map draws one cell's terrain from the batch that holds it. Most of Vvardenfell has
    // negative cell coordinates, so the batch must be found by rounding down, not toward zero.

    TEST(TerrainBatchIndex, a_batch_of_one_cell_is_the_cell_itself)
    {
        EXPECT_EQ(batchIndex(-9, 1), -9);
        EXPECT_EQ(batchIndex(0, 1), 0);
        EXPECT_EQ(batchIndex(5, 1), 5);
    }

    TEST(TerrainBatchIndex, negative_cells_round_down)
    {
        EXPECT_EQ(batchIndex(-1, 2), -1);
        EXPECT_EQ(batchIndex(-2, 2), -1);
        EXPECT_EQ(batchIndex(-9, 2), -5);
        EXPECT_EQ(batchIndex(-2, 4), -1);
        EXPECT_EQ(batchIndex(-4, 4), -1);
        EXPECT_EQ(batchIndex(-5, 4), -2);
    }

    TEST(TerrainBatchIndex, positive_cells_round_down)
    {
        EXPECT_EQ(batchIndex(0, 2), 0);
        EXPECT_EQ(batchIndex(1, 2), 0);
        EXPECT_EQ(batchIndex(2, 2), 1);
        EXPECT_EQ(batchIndex(3, 4), 0);
        EXPECT_EQ(batchIndex(4, 4), 1);
    }

    TEST(TerrainBatchIndex, every_cell_lies_inside_its_batch)
    {
        for (const int batchSize : { 1, 2, 4, 8, 16 })
        {
            for (int cell = -64; cell <= 64; ++cell)
            {
                const int batch = batchIndex(cell, batchSize);
                EXPECT_LE(batch * batchSize, cell) << cell << " in batches of " << batchSize;
                EXPECT_LT(cell, (batch + 1) * batchSize) << cell << " in batches of " << batchSize;
            }
        }
    }
}
