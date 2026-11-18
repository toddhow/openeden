// Task 0308 (docs/tasks/0308-skyrim-main-quest-whiterun.md): the Whiterun part of the main quest. Groups:
//  * SkyrimMq308Package: a ForceGreet package whose data names the topic subtype HELO (Irileth's forced greeting);
//  * SkyrimMq308StoryAid: the developer aid `story test quest` with a per-entry delay (`<hex>:<stage>@<seconds>`).
// Generated plugins; no game data.
#include <game_skyrimse/main_quest.hpp>
#include <game_skyrimse/package.hpp>

#include <gtest/gtest.h>

#include <string>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    PackageRecords parseOne(const Bytes& pack, std::uint32_t id)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("PACK"), 0, record4("PACK", 0u, id, pack)));
        PackageRecords records;
        Content::Error error;
        EXPECT_TRUE(parsePackageRecordsBytes(bytes.str(), "test.esp", order(), PackageLimits(), records, error))
            << error.mMessage;
        return records;
    }

    TEST(SkyrimMq308Package, aSubtypeHelloTopicMakesTheGreetingTheForcedOne)
    {
        Bytes pack;
        pack.append(sub4("EDID", Bytes().zstring("ForceGreetHello")));
        pack.append(sub4("PDTO", Bytes().u32(1).u32(0x4F4C4548u))); // type 1: a subtype, 'HELO'
        pack.append(sub4("PDTO", Bytes().u32(0).u32(0)));
        const PackageRecords records = parseOne(pack, 0x01000020u);
        ASSERT_EQ(records.mPackages.size(), 1u);
        EXPECT_EQ(records.mPackages[0].mForceGreetTopic, kForceGreetHelloTopic);
        EXPECT_EQ(records.mOddSubrecords, 0u);
    }

    TEST(SkyrimMq308Package, aSubtypeOtherThanHelloIsNoForcedGreeting)
    {
        Bytes pack;
        pack.append(sub4("EDID", Bytes().zstring("ForceGreetOther")));
        pack.append(sub4("PDTO", Bytes().u32(1).u32(0x544e4948u))); // 'HINT'
        const PackageRecords records = parseOne(pack, 0x01000021u);
        ASSERT_EQ(records.mPackages.size(), 1u);
        EXPECT_EQ(records.mPackages[0].mForceGreetTopic, kNullFormId);
    }

    TEST(SkyrimMq308Package, aRealTopicStillWinsOverALaterSubtype)
    {
        Bytes pack;
        pack.append(sub4("EDID", Bytes().zstring("ForceGreetTopicFirst")));
        pack.append(sub4("PDTO", Bytes().u32(0).u32(0x01000000 | 0x3E50)));
        pack.append(sub4("PDTO", Bytes().u32(1).u32(0x4F4C4548u)));
        const PackageRecords records = parseOne(pack, 0x01000022u);
        ASSERT_EQ(records.mPackages.size(), 1u);
        EXPECT_EQ(records.mPackages[0].mForceGreetTopic, 0x01000000u | 0x3E50u);
    }

    TEST(SkyrimMq308StoryAid, anEntryHasAQuestAStageAndADelay)
    {
        const auto entries = mainquest::parseStoryTestQuests("4E50D:30,0D0800:180@90.5,2610C@5");
        ASSERT_EQ(entries.size(), 3u);
        EXPECT_EQ(entries[0].mQuest, 0x4E50Du);
        EXPECT_EQ(entries[0].mStage, 30);
        EXPECT_EQ(entries[0].mAfter, 0.0);
        EXPECT_EQ(entries[1].mQuest, 0xD0800u);
        EXPECT_EQ(entries[1].mStage, 180);
        EXPECT_EQ(entries[1].mAfter, 90.5);
        EXPECT_EQ(entries[2].mQuest, 0x2610Cu);
        EXPECT_EQ(entries[2].mStage, -1) << "no stage: the quest is only started";
        EXPECT_EQ(entries[2].mAfter, 5.0);
    }

    TEST(SkyrimMq308StoryAid, emptyPartsAndNegativeOrBadDelaysAreHarmless)
    {
        EXPECT_TRUE(mainquest::parseStoryTestQuests("").empty());
        const auto entries = mainquest::parseStoryTestQuests(",4E50D:10@-3,,4E50D:20@x");
        ASSERT_EQ(entries.size(), 2u);
        EXPECT_EQ(entries[0].mAfter, 0.0) << "a negative delay is no delay";
        EXPECT_EQ(entries[1].mAfter, 0.0) << "an unreadable delay is no delay";
    }
}
