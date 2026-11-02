// Task 0055 (stage S1a): the tile key. Pure code; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/tilekey.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr std::uint64_t tagShifted = std::uint64_t{ 0x10 } << 56;
    constexpr std::uint64_t morton44 = (std::uint64_t{ 1 } << 44) - 1;

    class Rng
    {
    public:
        explicit Rng(std::uint64_t seed)
            : mState(seed)
        {
        }

        std::uint64_t next()
        {
            mState ^= mState << 13;
            mState ^= mState >> 7;
            mState ^= mState << 17;
            return mState;
        }

        std::uint64_t below(std::uint64_t bound) { return next() % bound; }

    private:
        std::uint64_t mState;
    };

    // An independent, bit by bit Morton code.
    std::uint64_t referenceMorton(std::uint32_t x, std::uint32_t y)
    {
        std::uint64_t code = 0;
        for (unsigned bit = 0; bit < 22; ++bit)
        {
            code |= static_cast<std::uint64_t>((x >> bit) & 1u) << (2 * bit);
            code |= static_cast<std::uint64_t>((y >> bit) & 1u) << (2 * bit + 1);
        }
        return code;
    }

    std::uint64_t referenceKey(const TileId& id)
    {
        return tagShifted | (std::uint64_t{ id.mWorldspace } << 48) | (std::uint64_t{ id.mLevel } << 44)
            | referenceMorton(id.mX, id.mY);
    }

    TileId randomId(Rng& rng, std::uint32_t level)
    {
        TileId id;
        id.mWorldspace = static_cast<std::uint8_t>(rng.below(256));
        id.mLevel = static_cast<std::uint8_t>(level);
        const std::uint64_t bound = std::uint64_t{ 1 } << (22 - level);
        id.mX = static_cast<std::uint32_t>(rng.below(bound));
        id.mY = static_cast<std::uint32_t>(rng.below(bound));
        return id;
    }

    TEST(LodTileKey, pinned_values_derived_by_hand_from_the_layout_table)
    {
        struct Case
        {
            std::uint8_t mWorldspace;
            std::uint32_t mLevel;
            std::int32_t mCellX, mCellY;
            std::uint64_t mKey;
            std::uint32_t mX, mY;
        };
        constexpr std::int32_t minCell = -(1 << 21);
        constexpr std::int32_t maxCell = (1 << 21) - 1;
        const Case cases[] = {
            // (2^21 >> 1) = 2^20 for both: Morton bits 40 and 41.
            { 0, 1, 0, 0, 0x1000130000000000ull, 1u << 20, 1u << 20 },
            // 2^21 for both: Morton bits 42 and 43.
            { 0, 0, 0, 0, 0x10000C0000000000ull, 1u << 21, 1u << 21 },
            // 2^21 - 1 for both: Morton bits 0 to 41 all set.
            { 0, 0, -1, -1, 0x100003FFFFFFFFFFull, (1u << 21) - 1, (1u << 21) - 1 },
            // x = 2^21 + 1 (bits 0 and 21 -> Morton 0 and 42), y = 2^21 (bit 21 -> Morton 43), worldspace 7.
            { 7, 0, 1, 0, 0x10070C0000000001ull, (1u << 21) + 1, 1u << 21 },
            // y = 2^21 + 1: y bit 0 -> Morton bit 1.
            { 0, 0, 0, 1, 0x10000C0000000002ull, 1u << 21, (1u << 21) + 1 },
            // 2^21 >> 15 = 64 for both: Morton bits 12 and 13; level field 15.
            { 0, 15, 0, 0, 0x1000F00000003000ull, 64, 64 },
            // The smallest cell: biased 0, the key is the tag alone.
            { 0, 0, minCell, minCell, 0x1000000000000000ull, 0, 0 },
            // The largest cell: biased 2^22 - 1, 44 Morton bits all set.
            { 0, 0, maxCell, maxCell, 0x10000FFFFFFFFFFFull, (1u << 22) - 1, (1u << 22) - 1 },
            // (2^21 - 1) >> 1 = 2^20 - 1: Morton bits 0 to 39 all set.
            { 0, 1, -1, -1, 0x100010FFFFFFFFFFull, (1u << 20) - 1, (1u << 20) - 1 },
            // Level 2, cell (3, 5): x = 2^19, y = 2^19 + 1; worldspace 2.
            { 2, 2, 3, 5, 0x100220C000000002ull, 1u << 19, (1u << 19) + 1 },
            // The top of level 15: 127 for both (Morton 0x3FFF), worldspace 255.
            { 255, 15, maxCell, maxCell, 0x10FFF00000003FFFull, 127, 127 },
        };
        for (const Case& c : cases)
        {
            SCOPED_TRACE(testing::Message() << "level " << c.mLevel << " cell " << c.mCellX << "," << c.mCellY);
            const std::optional<TileId> id = tileContainingCell(c.mWorldspace, c.mLevel, c.mCellX, c.mCellY);
            ASSERT_TRUE(id.has_value());
            EXPECT_EQ(id->mWorldspace, c.mWorldspace);
            EXPECT_EQ(id->mLevel, c.mLevel);
            EXPECT_EQ(id->mX, c.mX);
            EXPECT_EQ(id->mY, c.mY);
            const std::optional<std::uint64_t> key = packTileKey(*id);
            ASSERT_TRUE(key.has_value());
            EXPECT_EQ(*key, c.mKey);
            const std::optional<TileId> back = unpackTileKey(c.mKey);
            ASSERT_TRUE(back.has_value());
            EXPECT_EQ(*back, *id);
        }
    }

    TEST(LodTileKey, tag_is_0x10_in_the_top_byte_and_the_key_is_never_zero)
    {
        Rng rng(0x1234567ull);
        for (unsigned i = 0; i < 4000; ++i)
        {
            const TileId id = randomId(rng, i % 16);
            const std::optional<std::uint64_t> key = packTileKey(id);
            ASSERT_TRUE(key.has_value());
            EXPECT_EQ(*key >> 56, 0x10u);
            EXPECT_EQ(*key, referenceKey(id));
            EXPECT_NE(*key, 0u);
        }
        EXPECT_EQ(tagTerrainTile, 0x10);
        EXPECT_FALSE(unpackTileKey(0).has_value());
    }

    TEST(LodTileKey, round_trip_over_every_level_and_a_deterministic_sweep)
    {
        Rng rng(0xABCDEF01ull);
        for (std::uint32_t level = 0; level <= maxLevel; ++level)
        {
            for (unsigned i = 0; i < 300; ++i)
            {
                const TileId id = randomId(rng, level);
                const std::optional<std::uint64_t> key = packTileKey(id);
                ASSERT_TRUE(key.has_value());
                const std::optional<TileId> back = unpackTileKey(*key);
                ASSERT_TRUE(back.has_value());
                EXPECT_EQ(*back, id);
            }
            // The corners of the level.
            const std::uint32_t top = (1u << (22 - level)) - 1;
            const std::uint8_t level8 = static_cast<std::uint8_t>(level);
            for (const TileId& corner : { TileId{ 0, level8, 0, 0 }, TileId{ 255, level8, top, top },
                     TileId{ 1, level8, top, 0 }, TileId{ 1, level8, 0, top } })
            {
                const std::optional<std::uint64_t> key = packTileKey(corner);
                ASSERT_TRUE(key.has_value());
                EXPECT_EQ(unpackTileKey(*key), corner);
            }
        }
    }

    TEST(LodTileKey, order_within_a_level_is_morton_order)
    {
        std::vector<TileId> ids;
        for (std::uint32_t y = 0; y < 16; ++y)
        {
            for (std::uint32_t x = 0; x < 16; ++x)
            {
                ids.push_back(TileId{ 3, 4, 1000 + x * 7, 5000 + y * 5 });
            }
        }
        std::vector<TileId> byKey = ids;
        std::vector<TileId> byMorton = ids;
        std::sort(byKey.begin(), byKey.end(),
            [](const TileId& a, const TileId& b) { return *packTileKey(a) < *packTileKey(b); });
        std::sort(byMorton.begin(), byMorton.end(),
            [](const TileId& a, const TileId& b) { return referenceMorton(a.mX, a.mY) < referenceMorton(b.mX, b.mY); });
        EXPECT_EQ(byKey, byMorton);
        // x is the lower bit of each pair: (0,0) < (1,0) < (0,1) < (1,1) < (2,0).
        const auto keyOf = [](std::uint32_t x, std::uint32_t y) { return *packTileKey(TileId{ 0, 0, x, y }); };
        EXPECT_LT(keyOf(0, 0), keyOf(1, 0));
        EXPECT_LT(keyOf(1, 0), keyOf(0, 1));
        EXPECT_LT(keyOf(0, 1), keyOf(1, 1));
        EXPECT_LT(keyOf(1, 1), keyOf(2, 0));
    }

    TEST(LodTileKey, level_and_worldspace_dominate_the_order)
    {
        const std::uint64_t lowLevel = *packTileKey(TileId{ 0, 0, 100, 100 });
        const std::uint64_t highLevel = *packTileKey(TileId{ 0, 1, 0, 0 });
        const std::uint64_t otherWorld = *packTileKey(TileId{ 1, 0, 0, 0 });
        EXPECT_LT(lowLevel, highLevel);
        EXPECT_LT(highLevel, otherWorld);
    }

    TEST(LodTileKey, morton_encode_and_decode)
    {
        EXPECT_EQ(mortonEncode(0, 0), 0u);
        EXPECT_EQ(mortonEncode(1, 0), 1u);
        EXPECT_EQ(mortonEncode(0, 1), 2u);
        EXPECT_EQ(mortonEncode(1, 1), 3u);
        EXPECT_EQ(mortonEncode(2, 0), 4u);
        EXPECT_EQ(mortonEncode(0, 2), 8u);
        EXPECT_EQ(mortonEncode(3, 3), 15u);
        EXPECT_EQ(mortonEncode((1u << 22) - 1, 0), 0x55555555555ull);
        EXPECT_EQ(mortonEncode(0, (1u << 22) - 1), 0xAAAAAAAAAAAull);
        EXPECT_EQ(mortonEncode((1u << 22) - 1, (1u << 22) - 1), 0xFFFFFFFFFFFull);
        // Bits above 22 are ignored.
        EXPECT_EQ(mortonEncode(1u << 22, 0), 0u);
        EXPECT_EQ(mortonEncode(0xFFFFFFFFu, 0), 0x55555555555ull);

        Rng rng(0x5EED5EEDull);
        for (unsigned i = 0; i < 20000; ++i)
        {
            const std::uint32_t x = static_cast<std::uint32_t>(rng.below(1u << 22));
            const std::uint32_t y = static_cast<std::uint32_t>(rng.below(1u << 22));
            const std::uint64_t code = mortonEncode(x, y);
            EXPECT_EQ(code, referenceMorton(x, y));
            EXPECT_LE(code, morton44);
            std::uint32_t backX = 0, backY = 0;
            mortonDecode(code, backX, backY);
            EXPECT_EQ(backX, x);
            EXPECT_EQ(backY, y);
        }
    }

    TEST(LodTileKey, parent_key_drops_the_two_lowest_morton_bits_and_adds_one_to_the_level)
    {
        Rng rng(0x9ABCDEFull);
        for (unsigned i = 0; i < 4000; ++i)
        {
            const std::uint32_t level = i % 15; // 0 to 14
            const TileId id = randomId(rng, level);
            const std::optional<TileId> parent = parentTile(id);
            ASSERT_TRUE(parent.has_value());
            EXPECT_EQ(parent->mWorldspace, id.mWorldspace);
            EXPECT_EQ(parent->mLevel, id.mLevel + 1);
            EXPECT_EQ(parent->mX, id.mX >> 1);
            EXPECT_EQ(parent->mY, id.mY >> 1);

            const std::uint64_t childKey = *packTileKey(id);
            const std::uint64_t parentKey = *packTileKey(*parent);
            const std::uint64_t upper = childKey & ~morton44 & ~(std::uint64_t{ 0xF } << 44);
            const std::uint64_t expected
                = upper | (std::uint64_t{ id.mLevel + 1u } << 44) | ((childKey & morton44) >> 2);
            EXPECT_EQ(parentKey, expected);
        }
    }

    TEST(LodTileKey, parent_of_the_top_level_is_empty)
    {
        EXPECT_FALSE(parentTile(TileId{ 0, 15, 3, 4 }).has_value());
    }

    TEST(LodTileKey, children_are_the_four_tiles_whose_parent_is_the_tile)
    {
        Rng rng(0x600DF00Dull);
        for (unsigned i = 0; i < 2000; ++i)
        {
            const std::uint32_t level = 1 + i % 15; // 1 to 15
            const TileId id = randomId(rng, level);
            TileId children[4];
            ASSERT_TRUE(childTiles(id, children));
            std::uint64_t previous = 0;
            for (unsigned c = 0; c < 4; ++c)
            {
                EXPECT_EQ(children[c].mWorldspace, id.mWorldspace);
                EXPECT_EQ(children[c].mLevel, id.mLevel - 1);
                EXPECT_EQ(children[c].mX, id.mX * 2 + (c & 1u));
                EXPECT_EQ(children[c].mY, id.mY * 2 + (c >> 1));
                EXPECT_EQ(parentTile(children[c]), id);
                const std::optional<std::uint64_t> key = packTileKey(children[c]);
                ASSERT_TRUE(key.has_value());
                if (c > 0)
                {
                    EXPECT_LT(previous, *key);
                }
                previous = *key;
            }
        }
    }

    TEST(LodTileKey, level_zero_has_no_children)
    {
        TileId children[4];
        EXPECT_FALSE(childTiles(TileId{ 0, 0, 5, 6 }, children));
    }

    TEST(LodTileKey, limits_of_the_22_bit_coordinates)
    {
        // Level 0: 2^22 values.
        EXPECT_TRUE(packTileKey(TileId{ 0, 0, (1u << 22) - 1, (1u << 22) - 1 }).has_value());
        EXPECT_FALSE(packTileKey(TileId{ 0, 0, 1u << 22, 0 }).has_value());
        EXPECT_FALSE(packTileKey(TileId{ 0, 0, 0, 1u << 22 }).has_value());
        EXPECT_FALSE(packTileKey(TileId{ 0, 0, 0xFFFFFFFFu, 0 }).has_value());
        // Level 14: below 2^8.
        EXPECT_TRUE(packTileKey(TileId{ 0, 14, 255, 255 }).has_value());
        EXPECT_FALSE(packTileKey(TileId{ 0, 14, 256, 0 }).has_value());
        // Level 15: below 2^7.
        EXPECT_TRUE(packTileKey(TileId{ 0, 15, 127, 127 }).has_value());
        EXPECT_FALSE(packTileKey(TileId{ 0, 15, 128, 0 }).has_value());
        EXPECT_FALSE(packTileKey(TileId{ 0, 15, 0, 128 }).has_value());
        // Level 16 does not exist.
        EXPECT_FALSE(packTileKey(TileId{ 0, 16, 0, 0 }).has_value());
        EXPECT_FALSE(packTileKey(TileId{ 0, 255, 0, 0 }).has_value());
    }

    TEST(LodTileKey, cells_at_the_edges_of_the_range)
    {
        constexpr std::int32_t minCell = -(1 << 21);
        constexpr std::int32_t maxCell = (1 << 21) - 1;
        const std::optional<TileId> low = tileContainingCell(0, 0, minCell, minCell);
        ASSERT_TRUE(low.has_value());
        EXPECT_EQ(low->mX, 0u);
        EXPECT_EQ(low->mY, 0u);
        const std::optional<TileId> high = tileContainingCell(0, 0, maxCell, maxCell);
        ASSERT_TRUE(high.has_value());
        EXPECT_EQ(high->mX, (1u << 22) - 1);
        EXPECT_EQ(high->mY, (1u << 22) - 1);

        EXPECT_FALSE(tileContainingCell(0, 0, minCell - 1, 0).has_value());
        EXPECT_FALSE(tileContainingCell(0, 0, 0, minCell - 1).has_value());
        EXPECT_FALSE(tileContainingCell(0, 0, maxCell + 1, 0).has_value());
        EXPECT_FALSE(tileContainingCell(0, 0, 0, maxCell + 1).has_value());
        EXPECT_FALSE(tileContainingCell(0, 16, 0, 0).has_value());
        EXPECT_FALSE(tileContainingCell(0, 100, 0, 0).has_value());

        // Every valid cell is a valid tile at every level up to 15.
        for (std::uint32_t level = 0; level <= maxLevel; ++level)
        {
            const std::optional<TileId> tile = tileContainingCell(9, level, maxCell, minCell);
            ASSERT_TRUE(tile.has_value());
            EXPECT_TRUE(packTileKey(*tile).has_value());
        }
    }

    TEST(LodTileKey, tile_origin_and_side_in_cells)
    {
        struct Case
        {
            std::uint32_t mLevel;
            std::int32_t mCellX, mCellY;
            std::int32_t mOriginX, mOriginY;
            std::uint32_t mSide;
        };
        const Case cases[] = {
            { 0, 5, -7, 5, -7, 1 },
            { 1, -1, 0, -2, 0, 2 },
            { 1, 3, 3, 2, 2, 2 },
            { 3, -9, 17, -16, 16, 8 },
            { 4, -1, -1, -16, -16, 16 },
            { 15, 5, 5, 0, 0, 32768 },
            { 15, -1, 0, -32768, 0, 32768 },
        };
        for (const Case& c : cases)
        {
            SCOPED_TRACE(testing::Message() << "level " << c.mLevel << " cell " << c.mCellX << "," << c.mCellY);
            const std::optional<TileId> id = tileContainingCell(0, c.mLevel, c.mCellX, c.mCellY);
            ASSERT_TRUE(id.has_value());
            std::int32_t originX = 12345, originY = 12345;
            tileOriginCell(*id, originX, originY);
            EXPECT_EQ(originX, c.mOriginX);
            EXPECT_EQ(originY, c.mOriginY);
            EXPECT_EQ(tileSideInCells(*id), c.mSide);
        }
    }

    TEST(LodTileKey, a_cell_lies_inside_its_tile_for_a_sweep)
    {
        Rng rng(0xCE11ull);
        for (unsigned i = 0; i < 5000; ++i)
        {
            const std::uint32_t level = static_cast<std::uint32_t>(rng.below(16));
            const std::int32_t cellX = static_cast<std::int32_t>(rng.below(1u << 22)) - (1 << 21);
            const std::int32_t cellY = static_cast<std::int32_t>(rng.below(1u << 22)) - (1 << 21);
            const std::optional<TileId> id = tileContainingCell(1, level, cellX, cellY);
            ASSERT_TRUE(id.has_value());
            std::int32_t originX = 0, originY = 0;
            tileOriginCell(*id, originX, originY);
            const std::int64_t side = tileSideInCells(*id);
            EXPECT_EQ(side, std::int64_t{ 1 } << level);
            EXPECT_LE(originX, cellX);
            EXPECT_LT(std::int64_t{ cellX }, originX + side);
            EXPECT_LE(originY, cellY);
            EXPECT_LT(std::int64_t{ cellY }, originY + side);
            EXPECT_EQ((std::int64_t{ originX } + cellBias) % side, 0);
        }
    }

    TEST(LodTileKey, unpack_refuses_a_wrong_tag)
    {
        const std::uint64_t key = *packTileKey(TileId{ 3, 2, 10, 20 });
        EXPECT_TRUE(unpackTileKey(key).has_value());
        for (const std::uint64_t tag : { std::uint64_t{ 0x00 }, std::uint64_t{ 0x11 }, std::uint64_t{ 0x01 },
                 std::uint64_t{ 0x20 }, std::uint64_t{ 0xFF }, std::uint64_t{ 0x18 } })
        {
            const std::uint64_t other = (key & ~(std::uint64_t{ 0xFF } << 56)) | (tag << 56);
            EXPECT_FALSE(unpackTileKey(other).has_value()) << "tag " << tag;
        }
    }

    TEST(LodTileKey, unpack_refuses_coordinates_out_of_range_for_the_level)
    {
        // Level 15 allows x and y below 2^7: x bit 7 is Morton bit 14, y bit 7 is Morton bit 15.
        const std::uint64_t base = tagShifted | (std::uint64_t{ 15 } << 44);
        EXPECT_TRUE(unpackTileKey(base | 0x3FFF).has_value());
        EXPECT_FALSE(unpackTileKey(base | (std::uint64_t{ 1 } << 14)).has_value());
        EXPECT_FALSE(unpackTileKey(base | (std::uint64_t{ 1 } << 15)).has_value());
        EXPECT_FALSE(unpackTileKey(base | (std::uint64_t{ 1 } << 43)).has_value());
        // Level 1 allows x and y below 2^21: x bit 21 is Morton bit 42.
        const std::uint64_t levelOne = tagShifted | (std::uint64_t{ 1 } << 44);
        EXPECT_TRUE(unpackTileKey(levelOne | ((std::uint64_t{ 1 } << 42) - 1)).has_value());
        EXPECT_FALSE(unpackTileKey(levelOne | (std::uint64_t{ 1 } << 42)).has_value());
        EXPECT_FALSE(unpackTileKey(levelOne | (std::uint64_t{ 1 } << 43)).has_value());
        // Level 0 allows everything the 44 bits can hold.
        EXPECT_TRUE(unpackTileKey(tagShifted | morton44).has_value());
    }
}
