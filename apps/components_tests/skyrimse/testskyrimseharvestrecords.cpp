// Task 0221: the harvest data of FLOR and TREE records (PFIG, PFPC, RNAM) decoded into ModelRecord::mHarvest. Generated
// plugins; no game data. Written first and run red against stubs (no `mHarvest` field: the file did not compile).
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <string>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0;

    Bytes header()
    {
        return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
    }

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes topGroup(const char (&type)[5], const Bytes& records)
    {
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC(type), kGrupTop, records));
        return bytes;
    }

    Bytes plant(
        const char (&type)[5], bool pfig, std::uint32_t ingredient, bool pfpc, bool rnam, const std::string& verb)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Plant")));
        subs.append(sub4("MODL", Bytes().zstring("Plants\\Plant01.nif")));
        if (pfig)
            subs.append(sub4("PFIG", Bytes().u32(ingredient)));
        if (pfpc)
            subs.append(sub4("PFPC", Bytes().u8(10).u8(20).u8(30).u8(40)));
        if (rnam)
            subs.append(sub4("RNAM", Bytes().zstring(verb)));
        return record4(type, 0, 0x01000A00, subs);
    }

    bool parse(const Bytes& bytes, ParsedPlugin& plugin)
    {
        Content::Error error;
        const bool ok = parseRecordsBytes(bytes.str(), "test.esp", order(), ParseLimits(), plugin, error);
        EXPECT_TRUE(ok) << error.describe();
        return ok;
    }

    TEST(SkyrimHarvestRecords, aFloraKeepsItsIngredientChancesAndVerb)
    {
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("FLOR", plant("FLOR", true, 0x01000555, true, true, "Pick")), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        const ModelRecord& flor = plugin.mModels[0];
        ASSERT_TRUE(flor.mHarvest.has_value());
        EXPECT_EQ(flor.mHarvest->mIngredient, 0x01000555u) << "resolved to a load order id";
        EXPECT_EQ(flor.mHarvest->mChances[0], 10);
        EXPECT_EQ(flor.mHarvest->mChances[1], 20);
        EXPECT_EQ(flor.mHarvest->mChances[2], 30);
        EXPECT_EQ(flor.mHarvest->mChances[3], 40);
        EXPECT_TRUE(flor.mHarvest->mActivateText.mPresent);
        EXPECT_EQ(flor.mHarvest->mActivateText.mText, "Pick");
        EXPECT_EQ(plugin.mOddSubrecords, 0u);
    }

    TEST(SkyrimHarvestRecords, aTreeKeepsItsIngredientAndChances)
    {
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("TREE", plant("TREE", true, 0x01000556, true, false, "")), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        ASSERT_TRUE(plugin.mModels[0].mHarvest.has_value());
        EXPECT_EQ(plugin.mModels[0].mHarvest->mIngredient, 0x01000556u);
        EXPECT_EQ(plugin.mModels[0].mHarvest->mChances[3], 40);
        EXPECT_FALSE(plugin.mModels[0].mHarvest->mActivateText.mPresent);
    }

    TEST(SkyrimHarvestRecords, aTreeIgnoresARnamItDoesNotHave)
    {
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("TREE", plant("TREE", true, 0x01000556, false, true, "Cut")), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        ASSERT_TRUE(plugin.mModels[0].mHarvest.has_value());
        EXPECT_FALSE(plugin.mModels[0].mHarvest->mActivateText.mPresent);
    }

    TEST(SkyrimHarvestRecords, aFloraWithNoHarvestSubrecordsHasNoHarvestData)
    {
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("FLOR", plant("FLOR", false, 0, false, false, "")), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_FALSE(plugin.mModels[0].mHarvest.has_value());
    }

    TEST(SkyrimHarvestRecords, otherModelRecordsNeverGetHarvestData)
    {
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("ACTI", plant("ACTI", true, 0x01000555, true, true, "Mine")), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_FALSE(plugin.mModels[0].mHarvest.has_value());
    }

    TEST(SkyrimHarvestRecords, aWrongSizedPfpcIsCountedAndKeepsTheRest)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Plant")));
        subs.append(sub4("MODL", Bytes().zstring("Plants\\Plant01.nif")));
        subs.append(sub4("PFIG", Bytes().u32(0x01000555)));
        subs.append(sub4("PFPC", Bytes().u8(1).u8(2).u8(3)));
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("FLOR", record4("FLOR", 0, 0x01000A00, subs)), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        ASSERT_TRUE(plugin.mModels[0].mHarvest.has_value());
        EXPECT_EQ(plugin.mModels[0].mHarvest->mIngredient, 0x01000555u);
        EXPECT_FALSE(plugin.mModels[0].mHarvest->mHasChances);
        EXPECT_EQ(plugin.mOddSubrecords, 1u);
    }

    TEST(SkyrimHarvestRecords, aNullIngredientIsKeptAsNull)
    {
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("FLOR", plant("FLOR", true, 0, true, true, "Harvest")), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        ASSERT_TRUE(plugin.mModels[0].mHarvest.has_value());
        EXPECT_EQ(plugin.mModels[0].mHarvest->mIngredient, kNullFormId);
    }
}
