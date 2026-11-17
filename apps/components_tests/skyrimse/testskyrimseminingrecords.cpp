// Task 0224: the ACTI activate text (RNAM) and the MESG text (DESC) decoded into ModelRecord. Generated plugins; no
// game data. Written before the fields existed (the file did not compile: the red run).
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <string>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

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
        bytes.append(group4(Content::fourCC(type), 0, records));
        return bytes;
    }

    bool parse(const Bytes& bytes, ParsedPlugin& plugin)
    {
        Content::Error error;
        const bool ok = parseRecordsBytes(bytes.str(), "test.esp", order(), ParseLimits(), plugin, error);
        EXPECT_TRUE(ok) << error.describe();
        return ok;
    }

    TEST(SkyrimMiningRecords, anActivatorKeepsItsActivateTextAndGetsNoHarvestData)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("MineOreIron01")));
        subs.append(sub4("MODL", Bytes().zstring("Dungeons\\Mines\\Ore\\MineOreIron01.nif")));
        subs.append(sub4("FULL", Bytes().zstring("Iron Ore Vein")));
        subs.append(sub4("RNAM", Bytes().zstring("Mine")));
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("ACTI", record4("ACTI", 0, 0x01000A00, subs)), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_TRUE(plugin.mModels[0].mActivateText.mPresent);
        EXPECT_EQ(plugin.mModels[0].mActivateText.mText, "Mine");
        EXPECT_FALSE(plugin.mModels[0].mHarvest.has_value());
    }

    TEST(SkyrimMiningRecords, aFloraRnamStaysWithItsHarvestDataNotTheActivatorField)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Plant")));
        subs.append(sub4("MODL", Bytes().zstring("Plants\\Plant01.nif")));
        subs.append(sub4("PFIG", Bytes().u32(0x01000555)));
        subs.append(sub4("RNAM", Bytes().zstring("Pick")));
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("FLOR", record4("FLOR", 0, 0x01000A00, subs)), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_FALSE(plugin.mModels[0].mActivateText.mPresent);
        ASSERT_TRUE(plugin.mModels[0].mHarvest.has_value());
        EXPECT_EQ(plugin.mModels[0].mHarvest->mActivateText.mText, "Pick");
    }

    TEST(SkyrimMiningRecords, aMessageKeepsItsDescription)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("PickaxeFailureMessage")));
        subs.append(sub4("DESC", Bytes().zstring("You need a pickaxe to use this.")));
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("MESG", record4("MESG", 0, 0x01000A01, subs)), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_EQ(plugin.mModels[0].mType, Content::fourCC("MESG"));
        EXPECT_EQ(plugin.mModels[0].mCommon.mEditorId, "PickaxeFailureMessage");
        EXPECT_TRUE(plugin.mModels[0].mDescription.mPresent);
        EXPECT_EQ(plugin.mModels[0].mDescription.mText, "You need a pickaxe to use this.");
    }

    TEST(SkyrimMiningRecords, otherModelRecordsHaveNoDescriptionOrActivateText)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Door")));
        subs.append(sub4("DESC", Bytes().zstring("not a message")));
        subs.append(sub4("RNAM", Bytes().zstring("Open")));
        ParsedPlugin plugin;
        ASSERT_TRUE(parse(topGroup("DOOR", record4("DOOR", 0, 0x01000A02, subs)), plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_FALSE(plugin.mModels[0].mDescription.mPresent);
        EXPECT_FALSE(plugin.mModels[0].mActivateText.mPresent);
    }
}
