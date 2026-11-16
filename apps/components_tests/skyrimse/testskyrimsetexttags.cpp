// Task 0228: the text tags of Skyrim dialogue (`<Global=RoomCost>`, `<Alias=X>`, `<BaseName>`...), the pure expander and
// its use by the conversation runtime on generated plugins. No game data.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/journal.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using journal::TagHooks;

    TagHooks fullHooks()
    {
        TagHooks h;
        h.mAlias = [](std::string_view a) { return a == "Staff" ? std::string("Ancano") : std::string(); };
        h.mAliasFemale = [](std::string_view a) { return a == "Staff" ? 0 : (a == "Maid" ? 1 : -1); };
        h.mGlobal = [](std::string_view n) -> std::optional<float> {
            if (n == "RoomCost")
                return 10.f;
            if (n == "Ratio")
                return 2.5f;
            return std::nullopt;
        };
        h.mBaseName = [] { return std::string("Hulda"); };
        h.mBribeCost = []() -> std::optional<float> { return 100.f; };
        h.mCrimeGold = []() -> std::optional<float> { return 40.f; };
        return h;
    }

    std::string run(const std::string& text, const TagHooks& hooks = fullHooks())
    {
        return journal::expandTags(text, hooks, std::string_view());
    }

    TEST(SkyrimTextTagsPure, aGlobalShowsAsAWholeNumberOrTwoPlaces)
    {
        EXPECT_EQ(run("It costs <Global=RoomCost> gold."), "It costs 10 gold.");
        EXPECT_EQ(run("Ratio <Global=Ratio>."), "Ratio 2.5.");
    }

    TEST(SkyrimTextTagsPure, anUnknownGlobalOrTagIsLeftOutInAConversationAndDotsInTheJournal)
    {
        EXPECT_EQ(run("a<Global=Nope>b<Whatever>c"), "abc");
        EXPECT_EQ(journal::expandTags("a<Global=RoomCost>b"), "a...b");
        EXPECT_EQ(journal::expandTags("a<Global=RoomCost>b", TagHooks(), "..."), "a...b");
    }

    TEST(SkyrimTextTagsPure, tagNamesIgnoreCase)
    {
        EXPECT_EQ(run("<global=RoomCost>|<BASENAME>|<bribecost>|<crimegold>"), "10|Hulda|100|40");
    }

    TEST(SkyrimTextTagsPure, aliasTagsUseTheResolverAndAnUnfilledAliasIsLeftOut)
    {
        EXPECT_EQ(run("Find <Alias=Staff> and <Alias.ShortName=Staff>, not <Alias=Ghost>."),
            "Find Ancano and Ancano, not .");
    }

    TEST(SkyrimTextTagsPure, pronounsFollowTheAliasSex)
    {
        EXPECT_EQ(run("<Alias.Pronoun=Staff> <Alias.PronounObj=Staff> <Alias.PronounPos=Staff> "
                      "<Alias.PronounPosObj=Staff> <Alias.PronounRef=Staff> <Alias.PronounCap=Staff>"),
            "he him his his himself He");
        EXPECT_EQ(
            run("<Alias.Pronoun=Maid> <Alias.PronounObj=Maid> <Alias.PronounPosObj=Maid> <Alias.PronounCap=Maid>"),
            "she her hers She");
        EXPECT_EQ(run("<Alias.Pronoun=Ghost> <Alias.PronounRef=Ghost>"), "they themselves");
    }

    TEST(SkyrimTextTagsPure, aTagWhoseHookIsMissingIsLeftOut)
    {
        TagHooks none;
        EXPECT_EQ(run("x<Global=RoomCost>y<BaseName>z", none), "xyz");
    }

    TEST(SkyrimTextTagsPure, strayBracketsAreDroppedAndALongTagIsNotATag)
    {
        EXPECT_EQ(run("a > b"), "a  b");
        EXPECT_EQ(run("a < b"), "a  b");
        EXPECT_EQ(run("<" + std::string(80, 'x') + ">end"), std::string(80, 'x') + "end");
    }

    TEST(SkyrimTextTagsPure, theJournalOverloadsAreAsBefore)
    {
        EXPECT_EQ(journal::expandTags("<Alias=Staff> <Global=X>"), "Staff ...");
        const journal::AliasNameResolver resolver
            = [](std::string_view a) { return a == "Staff" ? std::string("Ancano") : std::string(); };
        EXPECT_EQ(journal::expandTags("<Alias=Staff> <Alias.Name=Other> <Global=X>", resolver), "Ancano ... ...");
    }

    // ---- the runtime ----------------------------------------------------------------------------------------------

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kGlobalRoom = kSelf | 0x901;
    constexpr FormId kNpcHulda = kSelf | 0x200;
    constexpr FormId kQuestRent = kSelf | 0x400;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicRent = kSelf | 0x301;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes infoRecord(std::uint32_t id, const std::string& text)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        Bytes trdt;
        trdt.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        b.append(sub4("TRDT", trdt));
        b.append(sub4("NAM1", Bytes().zstring(text)));
        b.append(sub4("NAM2", Bytes().u8(0)));
        return record4("INFO", 0, id, b);
    }

    Bytes topic(std::uint32_t id, const std::string& name, const char* subtype, std::uint8_t category,
        const std::string& full, const std::string& line)
    {
        Bytes b;
        b.append(edid(name));
        if (!full.empty())
            b.append(sub4("FULL", Bytes().zstring(full)));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuestRent)));
        b.append(sub4("DATA", Bytes().u8(0).u8(category).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(1)));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, 7, infoRecord(id + 0x100, line)));
        return record;
    }

    Bytes plugin()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes dials = topic(kTopicHello, "Hello", "HELO", 7, "", "Welcome, I am <BaseName>. Bribe: <BribeCost>.");
        dials.append(topic(kTopicRent, "Rent", "CUST", 0, "Room for <Global=RoomCost> gold",
            "That is <Global=RoomCost> septims, <Alias=Guest> <Alias.Pronoun=Guest>."));
        bytes.append(group4(Content::fourCC("DIAL"), 0, dials));
        Bytes glob;
        glob.append(edid("RoomCost"));
        glob.append(sub4("FNAM", Bytes().u8('f')));
        glob.append(sub4("FLTV", Bytes().f32(10.f)));
        bytes.append(group4(Content::fourCC("GLOB"), 0, record4("GLOB", 0, kGlobalRoom, glob)));
        Bytes npc;
        npc.append(edid("Hulda"));
        npc.append(sub4("FULL", Bytes().zstring("Hulda the Innkeeper")));
        npc.append(
            sub4("ACBS", Bytes().u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0)));
        npc.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        npc.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        bytes.append(group4(Content::fourCC("NPC_"), 0, record4("NPC_", 0, kNpcHulda, npc)));
        Bytes race;
        race.append(edid("NordRace"));
        race.append(sub4("VTCK", Bytes().u32(kSelf | 0x803).u32(kSelf | 0x804)));
        bytes.append(group4(Content::fourCC("RACE"), 0, record4("RACE", 0, kRaceNord, race)));
        return bytes;
    }

    struct Talk
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActorRecords;
        ActorIndex mActors;
        DialogueIndex mIndex;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        Talk()
        {
            const Bytes bytes = plugin();
            Content::Error error;
            EXPECT_TRUE(parseDialogueRecordsBytes(bytes.str(), "test.esp", order(), DialogueLimits(), mDialogue, error))
                << error.mMessage;
            mPlugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            Content::Error e2;
            EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), "test.esp", order(), ActorLimits(), mActorRecords, e2))
                << e2.mMessage;
            mActors.add(mActorRecords);
            mIndex.add(mDialogue, FormIdResolver(mPlugin->header(), "test.esp", order()));
            mRuntime = std::make_unique<DialogueRuntime>(mActors, mIndex, mQuests, QuestState::initial(mQuests),
                [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; });
        }
    };

    DialogueSpeaker hulda()
    {
        DialogueSpeaker s;
        s.mBase = kNpcHulda;
        s.mReference = kNpcHulda | 0x1000;
        return s;
    }

    TEST(SkyrimTextTagsRuntime, theGreetingAndTheTopicListFillGlobalsAndTheSpeaker)
    {
        Talk talk;
        openeden::sdk::game::DialogueTurn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(hulda(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Welcome, I am Hulda the Innkeeper. Bribe: 100.");
        ASSERT_EQ(turn.topics.size(), 1u);
        EXPECT_EQ(turn.topics[0].text, "Room for 10 gold");
    }

    TEST(SkyrimTextTagsRuntime, aChosenTopicFillsAliasesThroughTheTagSources)
    {
        Talk talk;
        DialogueTagSources sources;
        FormId asked = 0;
        sources.mAliasName = [&asked](FormId quest, std::string_view alias) {
            asked = quest;
            return alias == "Guest" ? std::string("Lucia") : std::string();
        };
        sources.mAliasFemale = [](FormId, std::string_view) { return 1; };
        talk.mRuntime->setTagSources(sources);
        openeden::sdk::game::DialogueTurn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(hulda(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(turn.topics[0].id, reply, error)) << error;
        EXPECT_EQ(reply.text, "That is 10 septims, Lucia she.");
        EXPECT_EQ(asked, kQuestRent);
    }

    TEST(SkyrimTextTagsRuntime, withoutSourcesAnAliasTagIsLeftOutAndNothingBreaks)
    {
        Talk talk;
        openeden::sdk::game::DialogueTurn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(hulda(), turn, error));
        ASSERT_TRUE(talk.mRuntime->choose(turn.topics[0].id, reply, error));
        EXPECT_EQ(reply.text, "That is 10 septims,  .");
    }
}
