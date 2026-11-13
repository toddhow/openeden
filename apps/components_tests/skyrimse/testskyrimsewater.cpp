// Task 0129 (M-S2 water): the Skyrim SE index answers the water of an exterior grid square as SDK values.
// Generated plugins only; the data rules are the ones task 0117 settled (the maximum float is the world default, an
// integer-looking or -FLT_MAX value means no water, DNAM's second float is the world's default water height).
#include <game_skyrimse/terrain_source.hpp>

#include <gtest/gtest.h>

#include <cfloat>
#include <limits>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    constexpr std::uint32_t kGrupWorld = 1, kGrupExtBlock = 4, kGrupExtSub = 5;
    constexpr std::uint32_t kDeleted = 0x20;
    constexpr std::uint32_t kParent = 0x01000900, kChild = 0x01000901, kLandOnly = 0x01000902, kDry = 0x01000903;
    constexpr float kOcean = -14000.0f;

    Bytes cell(std::uint32_t id, std::int32_t x, std::int32_t y, const std::vector<float>& xclw, std::uint32_t flags = 0)
    {
        Bytes subs = sub4("DATA", Bytes().u16(0x0002));
        subs.append(sub4("XCLC", Bytes().u32(static_cast<std::uint32_t>(x)).u32(static_cast<std::uint32_t>(y)).u32(0)));
        for (float w : xclw)
            subs.append(sub4("XCLW", Bytes().f32(w)));
        return record4("CELL", flags, id, subs);
    }

    Bytes world(std::uint32_t id, const Bytes& worldSubs, const Bytes& cells)
    {
        Bytes record = record4("WRLD", 0, id, worldSubs);
        record.append(group4(id, kGrupWorld, group4(0, kGrupExtBlock, group4(0, kGrupExtSub, cells))));
        return group4(Content::fourCC("WRLD"), 0, record);
    }

    Bytes worldSubs(const char* edid, std::uint32_t parent, std::uint16_t usage, float landHeight, float waterHeight,
        bool dnam = true)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring(edid)));
        if (parent != 0)
        {
            subs.append(sub4("WNAM", Bytes().u32(parent)));
            subs.append(sub4("PNAM", Bytes().u16(usage)));
        }
        if (dnam)
            subs.append(sub4("DNAM", Bytes().f32(landHeight).f32(waterHeight)));
        return subs;
    }

    Bytes plugin()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        const float noWater = -FLT_MAX;
        Bytes parentCells = cell(0x01000A00, 0, 0, { 500.0f });
        parentCells.append(cell(0x01000A01, 1, 0, { std::numeric_limits<float>::max() }));
        parentCells.append(cell(0x01000A02, 2, 0, { noWater }));
        parentCells.append(cell(0x01000A03, 3, 0, {}));
        parentCells.append(cell(0x01000A04, 4, 0, { 2147483648.0f }));
        bytes.append(world(kParent, worldSubs("P", 0, 0, -2048.0f, kOcean), parentCells));
        // The child lends land and water: no DNAM of its own.
        bytes.append(world(kChild, worldSubs("C", kParent, 0x09, 0, 0, false), cell(0x01000B00, 5, 5, { 900.0f })));
        // A child that lends land only and has a water height of its own.
        bytes.append(world(kLandOnly, worldSubs("D", kParent, 0x01, 0.0f, 100.0f), cell(0x01000C00, 6, 6, {})));
        // A world whose default is the "no water" sentinel.
        bytes.append(world(kDry, worldSubs("E", 0, 0, 0.0f, noWater), cell(0x01000D00, 0, 0, {})));
        return bytes;
    }

    // A later plugin: replaces the parent's (0,0) with another height and deletes the cell (1,0).
    Bytes overrider()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 }, { "test.esp", 0 } }, 24, 44);
        Bytes cells = cell(0x01000A00, 0, 0, { 700.0f });
        cells.append(cell(0x01000A02, 2, 0, { 50.0f }, kDeleted));
        bytes.append(world(kParent, sub4("EDID", Bytes().zstring("P")), cells));
        return bytes;
    }

    LoadOrder orderOf(bool second)
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        if (second)
        {
            EXPECT_TRUE(order.add("over.esp", false, err));
        }
        return order;
    }

    struct Fixture
    {
        SkyrimTerrainIndex mIndex;
        explicit Fixture(bool both = false)
        {
            std::string error;
            EXPECT_TRUE(mIndex.addPluginBytes(plugin().str(), "test.esp", orderOf(both), error)) << error;
            if (both)
            {
                EXPECT_TRUE(mIndex.addPluginBytes(overrider().str(), "over.esp", orderOf(true), error)) << error;
            }
        }
        sdk::CellWater water(std::uint32_t world, std::int32_t x, std::int32_t y, bool* known = nullptr) const
        {
            sdk::CellWater out;
            const bool ok = mIndex.cellWater(world, x, y, out);
            if (known)
                *known = ok;
            return out;
        }
    };

    void expectWater(const sdk::CellWater& w, float height)
    {
        EXPECT_TRUE(w.present);
        EXPECT_FLOAT_EQ(w.height, height);
    }
}

TEST(SkyrimWater, aCellsOwnHeightIsTheSurface)
{
    Fixture f;
    expectWater(f.water(kParent, 0, 0), 500.0f);
    expectWater(f.water(kChild, 5, 5), 900.0f);
}

TEST(SkyrimWater, theMaximumFloatAnAbsentXclwAndASquareWithNoCellTakeTheWorldDefault)
{
    Fixture f;
    expectWater(f.water(kParent, 1, 0), kOcean);
    expectWater(f.water(kParent, 3, 0), kOcean);
    expectWater(f.water(kParent, 40, -40), kOcean);
}

TEST(SkyrimWater, aNoWaterSentinelMeansNoSurface)
{
    Fixture f;
    bool known = false;
    EXPECT_FALSE(f.water(kParent, 2, 0, &known).present);
    EXPECT_TRUE(known);
    EXPECT_FALSE(f.water(kParent, 4, 0, &known).present); // 2^31, an integer looking sentinel
    EXPECT_TRUE(known);
}

TEST(SkyrimWater, aWorldWhoseDefaultIsTheSentinelHasNoWater)
{
    Fixture f;
    bool known = false;
    EXPECT_FALSE(f.water(kDry, 3, 3, &known).present);
    EXPECT_TRUE(known);
    EXPECT_FALSE(f.water(kDry, 0, 0).present);
}

TEST(SkyrimWater, aChildWithTheLandBitReadsTheParentsCellAndWithTheWaterBitTheParentsDefault)
{
    Fixture f;
    expectWater(f.water(kChild, 0, 0), 500.0f); // the parent's own cell
    expectWater(f.water(kChild, 7, 7), kOcean); // nothing listed: the inherited default
    EXPECT_FALSE(f.water(kChild, 2, 0).present); // the parent's no-water cell
}

TEST(SkyrimWater, aChildWithoutTheWaterBitUsesItsOwnDefault)
{
    Fixture f;
    expectWater(f.water(kLandOnly, 9, 9), 100.0f);
    expectWater(f.water(kLandOnly, 0, 0), 500.0f); // the land bit still lends the parent's cell
}

TEST(SkyrimWater, anUnknownWorldIsNotKnown)
{
    Fixture f;
    bool known = true;
    const sdk::CellWater w = f.water(0x01000FFF, 0, 0, &known);
    EXPECT_FALSE(known);
    EXPECT_FALSE(w.present);
}

TEST(SkyrimWater, aLaterPluginReplacesACellAndADeletedCellFallsBackToTheDefault)
{
    Fixture f(true);
    expectWater(f.water(kParent, 0, 0), 700.0f);
    // (2,0) was "no water" in the first plugin; the second deletes the cell, so the world default applies.
    expectWater(f.water(kParent, 2, 0), kOcean);
}
