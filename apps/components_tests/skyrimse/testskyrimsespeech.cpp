// Task 0155, milestone M-S4: speech checks and service stubs. Pure parts (actor value slots, the intimidate and bribe
// rules, the service classifier, the fragment script name in a VMAD) and the conversation runtime over generated
// plugins with a stand-in player. No game data. Fixtures are copied from testskyrimsedialogueruntime.cpp.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/speech.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;

    // load order: Skyrim.esm = 0, test.esp = 1, other.esp = 2. Ids 0x01xxxxxx are test.esp's own.
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kFaction = kSelf | 0x701;
    constexpr FormId kKeywordNpc = kSelf | 0x601;
    constexpr FormId kKeywordRace = kSelf | 0x602;
    constexpr FormId kVoice = kSelf | 0x801;
    constexpr FormId kVoiceList = kSelf | 0x802;
    constexpr FormId kGlobal = kSelf | 0x901;
    constexpr FormId kList = kSelf | 0xA01;
    constexpr FormId kNpcA = kSelf | 0x200;
    constexpr FormId kNpcB = kSelf | 0x201;
    constexpr FormId kTemplateNpc = kSelf | 0x202;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicHigh = kSelf | 0x301;
    constexpr FormId kQuest = kSelf | 0x400;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        EXPECT_TRUE(o.add("other.esp", false, err));
        return o;
    }

    Bytes header(const std::vector<Master>& masters)
    {
        return tes4Header(1.7f, 0, "A", "B", 1, masters, 24, 44);
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes top(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0,
        std::uint32_t runOn = 0, std::uint32_t reference = 0, std::uint8_t flags = 0, std::uint32_t p2 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>((compare << 5) | flags)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(p2).u32(runOn).u32(reference).u32(0);
        return sub4("CTDA", b);
    }

    Bytes trdt(std::uint8_t number, std::uint32_t emotionType = 0, std::uint32_t emotionValue = 50)
    {
        Bytes b;
        b.u32(emotionType).u32(emotionValue).u32(0).u8(number).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        return sub4("TRDT", b);
    }

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        std::uint16_t mFlags = 0;
        Bytes mConditions;
        std::vector<std::string> mTexts{ "Hello." };
        std::uint32_t mQuest = 0;
        std::uint32_t mSpeaker = 0;
        bool mDeleted = false;
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(s.mFlags).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        if (s.mQuest != 0)
            b.append(sub4("QNAM", Bytes().u32(s.mQuest)));
        if (s.mSpeaker != 0)
            b.append(sub4("ANAM", Bytes().u32(s.mSpeaker)));
        std::uint8_t n = 1;
        for (const std::string& t : s.mTexts)
        {
            b.append(trdt(n++));
            b.append(sub4("NAM1", Bytes().zstring(t)));
            b.append(sub4("NAM2", Bytes().u8(0)));
        }
        b.append(s.mConditions);
        return record4("INFO", s.mDeleted ? 0x20u : 0u, s.mId, b);
    }

    Bytes topicRecord(std::uint32_t id, const std::string& name, const std::string& subtype, float priority,
        std::uint32_t quest, std::uint8_t category = 7)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("PNAM", Bytes().f32(priority)));
        b.append(sub4("QNAM", Bytes().u32(quest)));
        b.append(sub4("BNAM", Bytes().u32(kSelf | 0x500)));
        b.append(sub4("DATA", Bytes().u8(0).u8(category).u16(73)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(1)));
        return record4("DIAL", 0, id, b);
    }

    /// DIAL + its type 7 group holding the infos.
    Bytes topicWithInfos(std::uint32_t id, const std::string& name, const std::string& subtype, float priority,
        std::uint32_t quest, const std::vector<InfoSpec>& infos)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b = topicRecord(id, name, subtype, priority, quest);
        b.append(group4(id, kGrupTopicChildren, children));
        return b;
    }

    Bytes acbs(std::uint32_t flags, std::uint16_t templateFlags)
    {
        Bytes b;
        b.u32(flags).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(templateFlags).u16(0).u16(0);
        return b;
    }

    Bytes npc(std::uint32_t id, const std::string& name, std::uint32_t flags, std::uint16_t templateFlags,
        std::uint32_t templ, const std::vector<std::uint32_t>& keywords, std::uint32_t voice, const Bytes& factions)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("ACBS", acbs(flags, templateFlags)));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        if (voice != 0)
            b.append(sub4("VTCK", Bytes().u32(voice)));
        if (templ != 0)
            b.append(sub4("TPLT", Bytes().u32(templ)));
        b.append(factions);
        if (!keywords.empty())
        {
            Bytes k;
            for (std::uint32_t w : keywords)
                k.u32(w);
            b.append(sub4("KWDA", k));
        }
        return record4("NPC_", 0, id, b);
    }

    Bytes faction(std::uint32_t id, std::int8_t rank)
    {
        return sub4("SNAM", Bytes().u32(id).u8(static_cast<std::uint8_t>(rank)).u8(0).u8(0).u8(0));
    }

    Bytes raceRecord()
    {
        Bytes b;
        b.append(edid("NordRace"));
        b.append(sub4("VTCK", Bytes().u32(kSelf | 0x803).u32(kSelf | 0x804)));
        b.append(sub4("KWDA", Bytes().u32(kKeywordRace)));
        return record4("RACE", 0, kRaceNord, b);
    }

    Bytes pluginBytes(const std::vector<Master>& masters, const Bytes& dials, const Bytes& npcs = Bytes(),
        const Bytes& extras = Bytes())
    {
        Bytes bytes = header(masters);
        if (dials.size() != 0)
            bytes.append(top("DIAL", dials));
        if (npcs.size() != 0)
        {
            bytes.append(top("NPC_", npcs));
            bytes.append(top("RACE", raceRecord()));
        }
        bytes.append(extras);
        return bytes;
    }

    std::vector<Master> masters()
    {
        return { { "Skyrim.esm", 0 } };
    }

    struct Loaded
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActors;
        bool mOk = false;
    };

    Loaded load(const Bytes& bytes, const std::string& name = "test.esp", DialogueLimits limits = DialogueLimits())
    {
        Loaded l;
        Content::Error error;
        l.mOk = parseDialogueRecordsBytes(bytes.str(), name, order(), limits, l.mDialogue, error);
        l.mPlugin = Content::Plugin::open(std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), name,
            error);
        Content::Error e2;
        EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), name, order(), ActorLimits(), l.mActors, e2)) << e2.mMessage;
        return l;
    }

    FormIdResolver resolverOf(const Loaded& l, const std::string& name = "test.esp")
    {
        return FormIdResolver(l.mPlugin->header(), name, order());
    }


    // ---- the conversation ----------------------------------------------------------------------------------------

    constexpr FormId kTopicRumors = kSelf | 0x310;
    constexpr FormId kTopicSecret = kSelf | 0x311;
    constexpr FormId kTopicOnce = kSelf | 0x312;
    constexpr FormId kTopicBye = kSelf | 0x313;
    constexpr FormId kTopicScript = kSelf | 0x314;
    constexpr FormId kTopicNoName = kSelf | 0x315;

    InfoSpec info(std::uint32_t local, const std::string& text, std::uint16_t flags = 0)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mTexts = { text };
        s.mFlags = flags;
        return s;
    }

    /// A player topic: category 0 with a FULL name.
    Bytes playerTopic(std::uint32_t id, const std::string& name, const std::vector<InfoSpec>& infos, bool named = true)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b;
        b.append(edid(name));
        if (named)
            b.append(sub4("FULL", Bytes().zstring(name)));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(0).u16(0)));
        b.append(sub4("SNAM", Bytes().text("CUST")));
        b.append(sub4("TIFC", Bytes().u32(static_cast<std::uint32_t>(infos.size()))));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, children));
        return record;
    }

    using Turn = openeden::sdk::game::DialogueTurn;

    struct Talk
    {
        Loaded mLoaded;
        ActorIndex mActors;
        DialogueIndex mDialogue;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        explicit Talk(const Bytes& bytes, DialogueRuntimeLimits limits = {})
        {
            mLoaded = load(bytes);
            mActors.add(mLoaded.mActors);
            mDialogue.add(mLoaded.mDialogue, resolverOf(mLoaded));
            mRuntime = std::make_unique<DialogueRuntime>(mActors, mDialogue, mQuests, QuestState::initial(mQuests),
                [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; }, limits);
        }

        static DialogueSpeaker speaker(FormId base)
        {
            DialogueSpeaker s;
            s.mBase = base;
            s.mReference = base | 0x1000;
            return s;
        }
    };


    std::vector<std::string> names(const Turn& turn)
    {
        std::vector<std::string> out;
        for (const auto& t : turn.topics)
            out.push_back(t.text);
        return out;
    }

    std::uint32_t idOf(const Turn& turn, const std::string& name)
    {
        for (const auto& t : turn.topics)
        {
            if (t.text == name)
                return t.id;
        }
        ADD_FAILURE() << "topic not offered: " << name;
        return 0;
    }

    // ---- pure parts -----------------------------------------------------------------------------------------------

    TEST(SkyrimSpeechPure, theSkillActorValuesAreTheSdkSlotsInOrder)
    {
        EXPECT_EQ(skillSlotOfActorValue(6), std::optional<std::size_t>(0));
        EXPECT_EQ(skillSlotOfActorValue(17), std::optional<std::size_t>(11)); // Speechcraft
        EXPECT_EQ(skillSlotOfActorValue(23), std::optional<std::size_t>(17));
        EXPECT_FALSE(skillSlotOfActorValue(5).has_value());
        EXPECT_FALSE(skillSlotOfActorValue(24).has_value());
    }

    TEST(SkyrimSpeechPure, theStandInPlayerAnswersSkillsAndVitals)
    {
        PlayerStandIn p;
        p.mSkills[11] = 40;
        p.mHealth = 250.f;
        EXPECT_EQ(actorValueOfPlayer(p, 17), 40.f);
        EXPECT_EQ(actorValueOfPlayer(p, 6), 15.f);
        EXPECT_EQ(actorValueOfPlayer(p, 24), 250.f);
        EXPECT_EQ(actorValueOfPlayer(p, 25), 100.f);
        EXPECT_EQ(actorValueOfPlayer(p, 999), 0.f);
    }

    TEST(SkyrimSpeechPure, anActorsValuesComeFromItsStats)
    {
        openeden::sdk::game::ActorStats s;
        s.skills[11] = 60;
        s.health = 321.f;
        s.stamina = 77.f;
        EXPECT_EQ(actorValueOfActor(s, 17), 60.f);
        EXPECT_EQ(actorValueOfActor(s, 24), 321.f);
        EXPECT_EQ(actorValueOfActor(s, 26), 77.f);
        EXPECT_EQ(actorValueOfActor(s, 25), 0.f);
    }

    TEST(SkyrimSpeechPure, intimidationNeedsAPlayerNoWeakerThanTheSpeaker)
    {
        EXPECT_TRUE(intimidateSucceeds(5, 5));
        EXPECT_TRUE(intimidateSucceeds(9, 5));
        EXPECT_FALSE(intimidateSucceeds(4, 5));
    }

    TEST(SkyrimSpeechPure, aBribeCostsGoldPerSpeakerLevelAndAtLeastOneLevel)
    {
        EXPECT_TRUE(bribeSucceeds(100.f, 5));
        EXPECT_FALSE(bribeSucceeds(99.f, 5));
        EXPECT_TRUE(bribeSucceeds(kBribeGoldPerLevel, 0));
        EXPECT_FALSE(bribeSucceeds(kBribeGoldPerLevel - 1.f, 0));
    }

    TEST(SkyrimSpeechPure, theServiceClassifierNamesTheMenusItKnows)
    {
        EXPECT_EQ(classifyService({ "foo", "showbartermenu" }), "barter");
        EXPECT_EQ(classifyService({ "showtrainingmenu" }), "training");
        EXPECT_EQ(classifyService({ "showgiftmenu" }), "gift");
        EXPECT_EQ(classifyService({ "rentroomscript", "foo" }), "rent a room");
        EXPECT_EQ(classifyService({ "foo", "bar" }), "");
        EXPECT_EQ(classifyService({}), "");
    }

    TEST(SkyrimSpeechPure, theFragmentScriptNameIsFoundInAVmad)
    {
        Bytes vmad;
        vmad.u16(5).u16(2).u16(0).u8(2).u8(1).u16(13).text("TIF__000A1B2C").u8(0).u16(0).u16(0);
        const std::string bytes = vmad.str();
        EXPECT_EQ(findFragmentScriptName({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }),
            "TIF__000A1B2C");
        Bytes none;
        none.u16(5).u16(2).u16(0).u16(13).text("QF_MyQuest_000");
        const std::string b2 = none.str();
        EXPECT_EQ(findFragmentScriptName({ reinterpret_cast<const std::byte*>(b2.data()), b2.size() }), "");
        Bytes bad;
        bad.u16(13).text("TIF__000A1BZZ");
        const std::string b3 = bad.str();
        EXPECT_EQ(findFragmentScriptName({ reinterpret_cast<const std::byte*>(b3.data()), b3.size() }), "");
    }

    // ---- the conversation with a stand-in player ---------------------------------------------------------------------

    constexpr FormId kTopicPersuade = kSelf | 0x330;
    constexpr FormId kTopicIntimidate = kSelf | 0x331;
    constexpr FormId kTopicBribe = kSelf | 0x332;
    constexpr FormId kTopicTrade = kSelf | 0x333;
    constexpr FormId kTopicPlain = kSelf | 0x334;
    constexpr FormId kTopicVeteran = kSelf | 0x335;

    Bytes vmadWithFragment(const std::string& script)
    {
        Bytes b;
        b.u16(5).u16(2).u16(0).u8(2).u8(1).u16(static_cast<std::uint16_t>(script.size())).text(script).u8(0);
        return sub4("VMAD", b);
    }

    Bytes speechPlugin()
    {
        Bytes dials;
        dials.append(topicWithInfos(kTopicHello, "Hello", "HELO", 1.0f, kQuest, { info(0x40, "Hello there.") }));
        // Persuade: the first response needs the player's Speechcraft (run on the target, the player) to be 25 or more,
        // the next one is the failure line.
        InfoSpec win = info(0x41, "You make a fair point.");
        win.mConditions = ctda(14, 3, 25.0f, 17, 1);
        dials.append(playerTopic(kTopicPersuade, "Persuade", { win, info(0x42, "Not convinced.") }));
        InfoSpec scared = info(0x43, "Please, no violence!");
        scared.mConditions = ctda(655, 0, 1.0f);
        dials.append(playerTopic(kTopicIntimidate, "Intimidate", { scared, info(0x44, "You do not scare me.") }));
        InfoSpec paid = info(0x45, "Fine, take it.");
        paid.mConditions = ctda(654, 0, 1.0f);
        InfoSpec refused = info(0x46, "Keep your coin.");
        dials.append(playerTopic(kTopicBribe, "Bribe", { paid, refused }));
        InfoSpec trade = info(0x47, "Let us see.");
        trade.mConditions = vmadWithFragment("TIF__000A1B2C");
        dials.append(playerTopic(kTopicTrade, "Let's trade.", { trade }));
        InfoSpec plain = info(0x48, "Nothing special.");
        plain.mConditions = vmadWithFragment("TIF__000A1B2D");
        dials.append(playerTopic(kTopicPlain, "Plain talk", { plain }));
        // A line for a seasoned player (level 3 or more), compared with the player's level (run on the target).
        InfoSpec veteran = info(0x49, "You have seen some battles.");
        veteran.mConditions = ctda(80, 3, 3.0f, 0, 1);
        dials.append(playerTopic(kTopicVeteran, "Battles", { veteran }));
        // Easy: needs a Speechcraft of 10, which a new game's stand-in player has without anyone setting it.
        InfoSpec easy = info(0x4A, "That is easy.");
        easy.mConditions = ctda(14, 3, 10.0f, 17, 1);
        dials.append(playerTopic(kSelf | 0x336, "Easy", { easy, info(0x4B, "Hard.") }));
        // Rent a room: the player's gold (Gold001 = Skyrim.esm 0x0F, run on the target) against a global.
        InfoSpec rent = info(0x4C, "Here is your key.");
        rent.mConditions = ctda(47, 3, 50.0f, 0x0F, 1);
        rent.mConditions.append(vmadWithFragment("TIF__000A1B2E"));
        InfoSpec noRent = info(0x4D, "You cannot afford it.");
        dials.append(playerTopic(kSelf | 0x337, "Rent", { rent, noRent }));
        Bytes npcs = npc(kNpcA, "Alice", NpcFlags::Female, 0, 0, {}, kVoice, faction(kFaction, 1));
        return pluginBytes(masters(), dials, npcs);
    }

    struct SpeechTalk : Talk
    {
        explicit SpeechTalk(PlayerStandIn player = PlayerStandIn())
            : Talk(speechPlugin())
        {
            mRuntime->setPlayer(player);
            mRuntime->setScriptProbe([](const std::string& script) {
                if (script == "TIF__000A1B2C")
                    return std::vector<std::string>{ "fragment_0", "showbartermenu", "akspeaker" };
                if (script == "TIF__000A1B2E")
                    return std::vector<std::string>{ "fragment_0", "rentroomscript" };
                return std::vector<std::string>{ "fragment_0", "akspeaker" };
            });
        }

        std::string say(const std::string& topic, Turn* keep = nullptr)
        {
            Turn greeting, reply;
            std::string error;
            EXPECT_TRUE(mRuntime->begin(Talk::speaker(kNpcA), greeting, error)) << error;
            EXPECT_TRUE(mRuntime->choose(idOf(greeting, topic), reply, error)) << error;
            if (keep != nullptr)
                *keep = reply;
            return reply.text;
        }
    };

    TEST(SkyrimSpeech, aPersuadeLineFollowsThePlayersSpeechcraft)
    {
        PlayerStandIn weak;
        weak.mSkills[11] = 24;
        EXPECT_EQ(SpeechTalk(weak).say("Persuade"), "Not convinced.");
        PlayerStandIn exact;
        exact.mSkills[11] = 25;
        EXPECT_EQ(SpeechTalk(exact).say("Persuade"), "You make a fair point.");
        PlayerStandIn strong;
        strong.mSkills[11] = 90;
        EXPECT_EQ(SpeechTalk(strong).say("Persuade"), "You make a fair point.");
    }

    TEST(SkyrimSpeech, theDefaultStandInPlayerHasANewGamesSpeechcraft)
    {
        EXPECT_EQ(SpeechTalk().say("Persuade"), "Not convinced."); // 15 < 25
    }

    TEST(SkyrimSpeech, anIntimidateLineSucceedsWhenThePlayerIsNoWeakerThanTheSpeaker)
    {
        PlayerStandIn novice; // level 1 against Alice's level 5
        EXPECT_EQ(SpeechTalk(novice).say("Intimidate"), "You do not scare me.");
        PlayerStandIn seasoned;
        seasoned.mLevel = 5;
        EXPECT_EQ(SpeechTalk(seasoned).say("Intimidate"), "Please, no violence!");
    }

    TEST(SkyrimSpeech, aBribeLineNeedsEnoughGold)
    {
        PlayerStandIn poor;
        poor.mGold = 99.f; // 20 gold a level, level 5
        EXPECT_EQ(SpeechTalk(poor).say("Bribe"), "Keep your coin.");
        PlayerStandIn rich;
        rich.mGold = 100.f;
        EXPECT_EQ(SpeechTalk(rich).say("Bribe"), "Fine, take it.");
    }

    TEST(SkyrimSpeech, aLevelConditionReadsThePlayersLevel)
    {
        PlayerStandIn low;
        Turn greeting;
        std::string error;
        SpeechTalk talkLow(low);
        ASSERT_TRUE(talkLow.mRuntime->begin(Talk::speaker(kNpcA), greeting, error)) << error;
        for (const std::string& n : names(greeting))
            EXPECT_NE(n, "Battles");
        PlayerStandIn high;
        high.mLevel = 3;
        SpeechTalk talkHigh(high);
        ASSERT_TRUE(talkHigh.mRuntime->begin(Talk::speaker(kNpcA), greeting, error)) << error;
        const std::vector<std::string> offered = names(greeting);
        EXPECT_NE(std::find(offered.begin(), offered.end(), "Battles"), offered.end());
    }

    TEST(SkyrimSpeech, theDefaultStandInPlayerIsWiredIntoTheConditionsWithoutASetter)
    {
        Talk talk(speechPlugin());
        Turn greeting, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), greeting, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(greeting, "Easy"), reply, error)) << error;
        EXPECT_EQ(reply.text, "That is easy."); // Speechcraft 15 >= 10
    }

    TEST(SkyrimSpeech, aGoldConditionReadsThePlayersGoldAndTheRentLineIsAService)
    {
        PlayerStandIn poor;
        poor.mGold = 49.f;
        Turn reply;
        EXPECT_EQ(SpeechTalk(poor).say("Rent", &reply), "You cannot afford it.");
        EXPECT_TRUE(reply.service.empty());
        PlayerStandIn enough;
        enough.mGold = 50.f;
        SpeechTalk talk(enough);
        EXPECT_EQ(talk.say("Rent", &reply), "Here is your key.");
        EXPECT_EQ(reply.service, "rent a room");
    }

    TEST(SkyrimSpeech, theAmuletOfArticulationTestIsAnsweredForTheNakedStandIn)
    {
        Talk talk(speechPlugin());
        PlayerStandIn player;
        SkyrimConditionWorld world(talk.mActors, talk.mDialogue);
        DialogueSpeaker speaker = Talk::speaker(kNpcA);
        world.setSpeaker(speaker);
        survey::Ctda equipped;
        equipped.mFunction = 182;
        equipped.mRunOn = 1;
        EXPECT_FALSE(world.answers(equipped)); // no player: nothing is known
        world.setPlayer(&player);
        EXPECT_TRUE(world.answers(equipped));
    }

    TEST(SkyrimSpeech, theStandInPlayerIsAlwaysThereByDefault)
    {
        Talk talk(speechPlugin()); // no setPlayer call
        EXPECT_EQ(talk.mRuntime->player().mLevel, 1);
        EXPECT_EQ(talk.mRuntime->player().mSkills[11], kStandInSkill);
    }

    TEST(SkyrimSpeech, aBarterLineIsAServiceThatEndsTheConversation)
    {
        SpeechTalk talk;
        Turn reply;
        talk.say("Let's trade.", &reply);
        EXPECT_EQ(reply.service, "barter");
        EXPECT_TRUE(reply.endsConversation);
        EXPECT_EQ(reply.text, "Let us see.");
        EXPECT_EQ(talk.mRuntime->stats().mServices, 1u);
        EXPECT_FALSE(talk.mRuntime->open());
    }

    TEST(SkyrimSpeech, aScriptedLineThatIsNoServiceStaysAnOrdinaryLine)
    {
        SpeechTalk talk;
        Turn reply;
        talk.say("Plain talk", &reply);
        EXPECT_TRUE(reply.service.empty());
        EXPECT_FALSE(reply.endsConversation);
        EXPECT_TRUE(reply.hadUnrunEffects);
    }

    TEST(SkyrimSpeech, withoutAScriptProbeNoLineIsAService)
    {
        Talk talk(speechPlugin());
        Turn greeting, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), greeting, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(greeting, "Let's trade."), reply, error)) << error;
        EXPECT_TRUE(reply.service.empty());
        EXPECT_FALSE(reply.endsConversation);
    }

    TEST(SkyrimSpeech, theScriptProbeIsAskedOncePerScript)
    {
        Talk talk(speechPlugin());
        int calls = 0;
        talk.mRuntime->setScriptProbe([&calls](const std::string&) {
            ++calls;
            return std::vector<std::string>{ "showbartermenu" };
        });
        Turn greeting, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), greeting, error));
        ASSERT_TRUE(talk.mRuntime->choose(idOf(greeting, "Let's trade."), reply, error));
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), greeting, error));
        ASSERT_TRUE(talk.mRuntime->choose(idOf(greeting, "Let's trade."), reply, error));
        EXPECT_EQ(calls, 1);
    }

    // ---- a demo plugin for the in-game check ------------------------------------------------------------------------

    /// Only the speech topics of speechPlugin(): no greeting, no NPC, no race (a race record of this plugin would
    /// override the real Nord race when the file is loaded after the masters).
    Bytes demoPlugin()
    {
        Bytes dials;
        InfoSpec win = info(0x41, "You make a fair point.");
        win.mConditions = ctda(14, 3, 25.0f, 17, 1);
        dials.append(playerTopic(kTopicPersuade, "Persuade me", { win, info(0x42, "Not convinced.") }));
        InfoSpec scared = info(0x43, "Please, no violence!");
        scared.mConditions = ctda(655, 0, 1.0f);
        dials.append(playerTopic(kTopicIntimidate, "Intimidate me", { scared, info(0x44, "You do not scare me.") }));
        InfoSpec paid = info(0x45, "Fine, take it.");
        paid.mConditions = ctda(654, 0, 1.0f);
        dials.append(playerTopic(kTopicBribe, "Bribe me", { paid, info(0x46, "Keep your coin.") }));
        Bytes bytes = header(masters());
        bytes.append(top("DIAL", dials));
        return bytes;
    }

    // Writes the demo plugin for tools/neutral_session_check when OPENEDEN_SPEECH_DEMO_OUT names the file.
    TEST(SkyrimSpeechDemo, writesThePluginWhenAsked)
    {
        const char* out = std::getenv("OPENEDEN_SPEECH_DEMO_OUT");
        if (out == nullptr || *out == 0)
            GTEST_SKIP() << "OPENEDEN_SPEECH_DEMO_OUT is not set";
        std::ofstream file(out, std::ios::binary);
        const std::string bytes = demoPlugin().str();
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        EXPECT_TRUE(file.good());
    }
}
