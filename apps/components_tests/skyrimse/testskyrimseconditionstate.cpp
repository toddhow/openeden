// Task 0156: Skyrim SE condition functions the neutral world can answer honestly (conversation and fresh-game state,
// locations, the player's sex and race, quest script variables from the .pex defaults). Generated records and scripts,
// no game data.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/quest_script.hpp>
#include <game_skyrimse/speech.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/pexassembler.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kLocTown = kSelf | 0x500;
    constexpr FormId kLocInn = kSelf | 0x501;
    constexpr FormId kLocOther = kSelf | 0x502;
    constexpr FormId kKeywordA = kSelf | 0x600;
    constexpr FormId kKeywordB = kSelf | 0x601;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kNpc = kSelf | 0x200;
    constexpr FormId kNordRace = 0x00013746;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    survey::Ctda cond(std::uint16_t function, std::uint32_t p1 = 0, std::uint32_t runOn = 0, std::uint8_t compare = 0,
        float value = 1.f)
    {
        survey::Ctda c;
        c.mFunction = function;
        c.mParam1 = p1;
        c.mRunOn = runOn;
        c.mCompare = compare;
        c.mComparison = value;
        return c;
    }

    struct Fixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        PlayerStandIn mPlayer;

        Fixture()
        {
            DialogueSpeaker s;
            s.mBase = kNpc;
            mWorld.setSpeaker(s);
        }

        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    // ---- conversation and fresh-game state ---------------------------------------------------------------------

    TEST(SkyrimConditionState, theSpeakerIsInDialogueOnlyWhenTheConversationSaysSo)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(cond(249)));
        EXPECT_FALSE(f.holds(cond(249)));
        f.mWorld.setConversationOpen(true);
        EXPECT_TRUE(f.mWorld.answers(cond(249)));
        EXPECT_TRUE(f.holds(cond(249)));
        f.mWorld.setConversationOpen(false);
        EXPECT_TRUE(f.mWorld.answers(cond(249)));
        EXPECT_FALSE(f.holds(cond(249)));
    }

    TEST(SkyrimConditionState, theSpeakerAndThePlayerAreAliveAndPeaceful)
    {
        Fixture f;
        f.mWorld.setPlayer(&f.mPlayer);
        for (const std::uint32_t runOn : { 0u, 1u })
        {
            EXPECT_TRUE(f.mWorld.answers(cond(46, 0, runOn))) << runOn; // GetDead
            EXPECT_FALSE(f.holds(cond(46, 0, runOn, 0, 1.f))) << runOn;
            EXPECT_TRUE(f.holds(cond(46, 0, runOn, 0, 0.f))) << runOn;
            EXPECT_TRUE(f.mWorld.answers(cond(289, 0, runOn))) << runOn; // IsInCombat
            EXPECT_TRUE(f.holds(cond(289, 0, runOn, 0, 0.f))) << runOn;
            EXPECT_TRUE(f.mWorld.answers(cond(49, 0, runOn))) << runOn; // GetSleeping
            EXPECT_TRUE(f.holds(cond(49, 0, runOn, 0, 0.f))) << runOn;
        }
        EXPECT_TRUE(f.mWorld.answers(cond(66, kNpc, 0))); // GetShouldAttack
        EXPECT_TRUE(f.holds(cond(66, kNpc, 0, 0, 0.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(453))); // GetPlayerTeammate
        EXPECT_TRUE(f.holds(cond(453, 0, 0, 0, 0.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(579, 0x1234))); // GetEquippedShout
        EXPECT_TRUE(f.holds(cond(579, 0x1234, 0, 0, 0.f)));
    }

    TEST(SkyrimConditionState, aFreshGameHasNoCrimeNobodyKilledAndNobodyTalkedTo)
    {
        Fixture f;
        f.mWorld.setPlayer(&f.mPlayer);
        for (const int fn : { 145, 144, 402, 375, 376, 459 })
        {
            EXPECT_TRUE(f.mWorld.answers(cond(static_cast<std::uint16_t>(fn), 0, 1))) << fn;
            EXPECT_TRUE(f.holds(cond(static_cast<std::uint16_t>(fn), 0, 1, 0, 0.f))) << fn;
            EXPECT_TRUE(f.mWorld.answers(cond(static_cast<std::uint16_t>(fn), 0, 0))) << fn;
        }
        EXPECT_TRUE(f.mWorld.answers(cond(84, 0x777))); // GetDeadCount
        EXPECT_TRUE(f.holds(cond(84, 0x777, 0, 0, 0.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(314))); // IsActorAVictim
        EXPECT_FALSE(f.holds(cond(314, 0, 0, 0, 1.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(50))); // GetTalkedToPC
        EXPECT_FALSE(f.holds(cond(50)));
        f.mWorld.setTalkedToPlayer(true);
        EXPECT_TRUE(f.holds(cond(50)));
    }

    TEST(SkyrimConditionState, theRandomPercentIsSeededAndDeterministic)
    {
        Fixture a, b, c;
        EXPECT_FALSE(a.mWorld.answers(cond(77))); // no seed: the old zero, not an answer
        EXPECT_EQ(a.mWorld.randomPercent(), 0.f);
        a.mWorld.setRandomSeed(42);
        b.mWorld.setRandomSeed(42);
        c.mWorld.setRandomSeed(43);
        EXPECT_TRUE(a.mWorld.answers(cond(77)));
        std::vector<float> va, vb, vc;
        for (int i = 0; i < 64; ++i)
        {
            va.push_back(a.mWorld.randomPercent());
            vb.push_back(b.mWorld.randomPercent());
            vc.push_back(c.mWorld.randomPercent());
        }
        EXPECT_EQ(va, vb);
        EXPECT_NE(va, vc);
        for (const float v : va)
        {
            EXPECT_GE(v, 0.f);
            EXPECT_LE(v, 99.f);
            EXPECT_EQ(v, static_cast<float>(static_cast<int>(v)));
        }
        EXPECT_GT(*std::max_element(va.begin(), va.end()), 50.f);
    }

    TEST(SkyrimConditionState, vitalsAreFullAndOtherValuesAreNotGuessed)
    {
        Fixture f;
        f.mWorld.setPlayer(&f.mPlayer);
        for (const std::uint32_t av : { kAvHealth, kAvMagicka, kAvStamina })
        {
            EXPECT_TRUE(f.mWorld.answers(cond(640, av, 1))) << av;
            EXPECT_TRUE(f.holds(cond(640, av, 1, 0, 1.f))) << av;
        }
        EXPECT_FALSE(f.mWorld.answers(cond(640, kAvSpeechcraft, 1)));
    }

    TEST(SkyrimConditionState, thePlayersSexAndRaceAreTheStandIns)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(cond(131))); // no stand-in: unknown
        f.mWorld.setPlayer(&f.mPlayer);
        EXPECT_TRUE(f.mWorld.answers(cond(131, 0)));
        EXPECT_TRUE(f.holds(cond(131, 0))); // male: GetPCIsSex 0 == 1
        EXPECT_FALSE(f.holds(cond(131, 1)));
        f.mPlayer.mFemale = true;
        EXPECT_FALSE(f.holds(cond(131, 0)));
        EXPECT_TRUE(f.holds(cond(131, 1)));
        EXPECT_TRUE(f.mWorld.answers(cond(130, kNordRace)));
        EXPECT_TRUE(f.holds(cond(130, kNordRace)));
        EXPECT_FALSE(f.holds(cond(130, kNordRace + 1)));
        EXPECT_TRUE(f.mWorld.answers(cond(70, 1, 1))); // GetIsSex run on the player
        EXPECT_TRUE(f.holds(cond(70, 1, 1)));
        EXPECT_TRUE(f.holds(cond(69, kNordRace, 1)));
    }

    TEST(SkyrimConditionState, interiorComesFromTheCellWhenGiven)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(cond(300)));
        DialogueSpeaker s;
        s.mBase = kNpc;
        s.mInterior = true;
        f.mWorld.setSpeaker(s);
        EXPECT_TRUE(f.mWorld.answers(cond(300)));
        EXPECT_TRUE(f.holds(cond(300)));
        s.mInterior = false;
        f.mWorld.setSpeaker(s);
        EXPECT_TRUE(f.mWorld.answers(cond(300)));
        EXPECT_FALSE(f.holds(cond(300)));
    }

    TEST(SkyrimConditionState, noInteriorIsInAnyWorldspace)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(cond(310, 0x13)));
        DialogueSpeaker s;
        s.mBase = kNpc;
        s.mInterior = true;
        f.mWorld.setSpeaker(s);
        EXPECT_TRUE(f.mWorld.answers(cond(310, 0x13))); // GetInWorldspace: an interior is in none
        EXPECT_FALSE(f.holds(cond(310, 0x13)));
        s.mInterior = false; // an exterior whose worldspace is not given stays unanswered
        f.mWorld.setSpeaker(s);
        EXPECT_FALSE(f.mWorld.answers(cond(310, 0x13)));
    }

    TEST(SkyrimConditionState, theKeywordDataOfALocationIsEmptyInAFreshGame)
    {
        Fixture f;
        EXPECT_TRUE(f.mWorld.answers(cond(606, kLocTown)));
        EXPECT_TRUE(f.holds(cond(606, kLocTown, 0, 0, 0.f)));
    }

    // ---- locations ---------------------------------------------------------------------------------------------

    Bytes lctn(FormId id, const std::string& editorId, FormId parent, const std::vector<FormId>& keywords)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring(editorId)));
        if (parent != 0)
            b.append(sub4("PNAM", Bytes().u32(parent)));
        if (!keywords.empty())
        {
            b.append(sub4("KSIZ", Bytes().u32(static_cast<std::uint32_t>(keywords.size()))));
            Bytes kw;
            for (const FormId k : keywords)
                kw.u32(k);
            b.append(sub4("KWDA", kw));
        }
        return record4("LCTN", 0, id, b);
    }

    LocationRecords parseLocations(const Bytes& records)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("LCTN"), 0, records));
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        EXPECT_NE(plugin, nullptr);
        LocationRecords out;
        EXPECT_TRUE(parseLocationRecords(*plugin, order(), LocationLimits(), out));
        return out;
    }

    TEST(SkyrimLocations, parentAndKeywordsAreRead)
    {
        Bytes recs = lctn(kLocTown, "Town", 0, { kKeywordA });
        recs.append(lctn(kLocInn, "Inn", kLocTown, { kKeywordA, kKeywordB }));
        const LocationRecords r = parseLocations(recs);
        ASSERT_EQ(r.mLocations.size(), 2u);
        EXPECT_EQ(r.mLocations[1].mCommon.mEditorId, "Inn");
        EXPECT_EQ(r.mLocations[1].mParent, kLocTown);
        EXPECT_EQ(r.mLocations[1].mKeywords.size(), 2u);
        EXPECT_EQ(r.mLocations[0].mParent, kNullFormId);
        EXPECT_EQ(r.mCount.mParsed, 2u);
        EXPECT_EQ(r.mCount.mFailed, 0u);
    }

    TEST(SkyrimLocations, aWrongSizedParentIsOddNotGuessed)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("Bad")));
        b.append(sub4("PNAM", Bytes().u16(3)));
        const LocationRecords r = parseLocations(record4("LCTN", 0, kLocTown, b));
        ASSERT_EQ(r.mLocations.size(), 1u);
        EXPECT_EQ(r.mLocations[0].mParent, kNullFormId);
        EXPECT_EQ(r.mOddSubrecords, 1u);
    }

    TEST(SkyrimLocations, theParentChainAnswersIsWithinAndStopsOnCycles)
    {
        Bytes recs = lctn(kLocTown, "Town", 0, { kKeywordA });
        recs.append(lctn(kLocInn, "Inn", kLocTown, { kKeywordB }));
        recs.append(lctn(kLocOther, "Other", kLocOther, {})); // its own parent: a cycle
        LocationIndex index;
        index.add(parseLocations(recs));
        EXPECT_TRUE(index.isWithin(kLocInn, kLocInn));
        EXPECT_TRUE(index.isWithin(kLocInn, kLocTown));
        EXPECT_FALSE(index.isWithin(kLocTown, kLocInn));
        EXPECT_FALSE(index.isWithin(kLocOther, kLocTown));
        EXPECT_FALSE(index.isWithin(kNullFormId, kLocTown));
        EXPECT_TRUE(index.hasKeyword(kLocInn, kKeywordB));
        EXPECT_FALSE(index.hasKeyword(kLocInn, kKeywordA)); // the location's own keywords only
    }

    TEST(SkyrimConditionState, theSpeakersLocationAnswersGetInCurrentLocAndLocationHasKeyword)
    {
        Bytes recs = lctn(kLocTown, "Town", 0, { kKeywordA });
        recs.append(lctn(kLocInn, "Inn", kLocTown, { kKeywordB }));
        LocationIndex index;
        index.add(parseLocations(recs));
        Fixture f;
        f.mWorld.setLocations(&index);
        DialogueSpeaker s;
        s.mBase = kNpc;
        f.mWorld.setSpeaker(s); // the cell's location is not known
        EXPECT_FALSE(f.mWorld.answers(cond(359, kLocTown)));
        EXPECT_FALSE(f.mWorld.answers(cond(562, kKeywordA)));
        s.mLocationKnown = true;
        s.mLocation = kLocInn;
        f.mWorld.setSpeaker(s);
        EXPECT_TRUE(f.mWorld.answers(cond(359, kLocTown)));
        EXPECT_TRUE(f.holds(cond(359, kLocTown)));
        EXPECT_TRUE(f.holds(cond(359, kLocInn)));
        EXPECT_FALSE(f.holds(cond(359, kLocOther)));
        EXPECT_TRUE(f.mWorld.answers(cond(562, kKeywordB)));
        EXPECT_TRUE(f.holds(cond(562, kKeywordB)));
        EXPECT_FALSE(f.holds(cond(562, kKeywordA)));
        // a cell with no location: the speaker is nowhere
        s.mLocation = kNullFormId;
        f.mWorld.setSpeaker(s);
        EXPECT_TRUE(f.mWorld.answers(cond(359, kLocTown)));
        EXPECT_FALSE(f.holds(cond(359, kLocTown)));
        // without an index nothing is answered
        Fixture g;
        s.mLocation = kLocInn;
        g.mWorld.setSpeaker(s);
        EXPECT_FALSE(g.mWorld.answers(cond(359, kLocTown)));
    }

    // ---- quest scripts and their variables ---------------------------------------------------------------------

    Bytes wstr(const std::string& s)
    {
        return Bytes().u16(static_cast<std::uint16_t>(s.size())).text(s);
    }

    Bytes vmad(const std::string& script, const Bytes& props, std::uint16_t propCount)
    {
        Bytes b;
        b.u16(5).u16(2).u16(1);
        b.append(wstr(script)).u8(0).u16(propCount).append(props);
        b.u32(0xDEADBEEF); // the fragments part, not read
        return sub4("VMAD", b);
    }

    TEST(SkyrimQuestScripts, theVmadScriptsAndTheirScalarPropertiesAreRead)
    {
        Bytes props;
        props.append(wstr("Counter")).u8(3).u8(1).u32(7);
        props.append(wstr("Ratio")).u8(4).u8(1).f32(2.5f);
        props.append(wstr("Flag")).u8(5).u8(1).u8(1);
        props.append(wstr("Target")).u8(1).u8(1).u32(0x1234).u16(0).u16(0); // an object: skipped, aligned
        props.append(wstr("Names")).u8(12).u8(1).u32(2).append(wstr("a")).append(wstr("bc"));
        props.append(wstr("After")).u8(3).u8(1).u32(9);
        const Bytes sub = vmad("MyQuest", props, 6);
        std::vector<QuestScriptRef> scripts;
        const std::string raw = sub.str().substr(6); // after the subrecord header
        EXPECT_TRUE(parseVmadScripts({ reinterpret_cast<const std::byte*>(raw.data()), raw.size() },
            QuestScriptLimits(), scripts));
        ASSERT_EQ(scripts.size(), 1u);
        EXPECT_EQ(scripts[0].mName, "MyQuest");
        ASSERT_EQ(scripts[0].mProperties.size(), 6u);
        EXPECT_TRUE(scripts[0].mProperties[0].mScalar);
        EXPECT_EQ(scripts[0].mProperties[0].mValue, 7.f);
        EXPECT_EQ(scripts[0].mProperties[1].mValue, 2.5f);
        EXPECT_EQ(scripts[0].mProperties[2].mValue, 1.f);
        EXPECT_FALSE(scripts[0].mProperties[3].mScalar);
        EXPECT_FALSE(scripts[0].mProperties[4].mScalar);
        EXPECT_EQ(scripts[0].mProperties[5].mValue, 9.f); // read after the array: the walk stayed aligned
    }

    TEST(SkyrimQuestScripts, aTruncatedVmadIsDamagedNotGuessed)
    {
        Bytes props;
        props.append(wstr("Counter")).u8(3).u8(1); // the value is missing
        const std::string raw = vmad("MyQuest", props, 1).str().substr(6, 20);
        std::vector<QuestScriptRef> scripts;
        EXPECT_FALSE(parseVmadScripts({ reinterpret_cast<const std::byte*>(raw.data()), raw.size() },
            QuestScriptLimits(), scripts));
    }

    QuestScriptIndex parseQuests(const Bytes& records, QuestScriptRecords* keep = nullptr)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("QUST"), 0, records));
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        EXPECT_NE(plugin, nullptr);
        QuestScriptRecords out;
        EXPECT_TRUE(parseQuestScripts(*plugin, order(), QuestScriptLimits(), out));
        if (keep != nullptr)
            *keep = out;
        QuestScriptIndex index;
        index.add(std::move(out));
        return index;
    }

    TEST(SkyrimQuestScripts, aQuestRecordGivesItsScriptsToTheIndex)
    {
        Bytes props;
        props.append(wstr("Counter")).u8(3).u8(1).u32(7);
        Bytes q;
        q.append(sub4("EDID", Bytes().zstring("MyQ")));
        q.append(vmad("MyQuest", props, 1));
        Bytes none;
        none.append(sub4("EDID", Bytes().zstring("NoScript")));
        Bytes recs = record4("QUST", 0, kQuest, q);
        recs.append(record4("QUST", 0, kQuest + 1, none));
        QuestScriptRecords r;
        const QuestScriptIndex index = parseQuests(recs, &r);
        EXPECT_EQ(r.mQuestsSeen, 2u);
        EXPECT_EQ(r.mWithScripts, 1u);
        ASSERT_NE(index.find(kQuest), nullptr);
        EXPECT_EQ(index.find(kQuest)->mScripts[0].mName, "MyQuest");
        ASSERT_NE(index.find(kQuest + 1), nullptr);
        EXPECT_TRUE(index.find(kQuest + 1)->mScripts.empty());
    }

    struct Scripts
    {
        std::map<std::string, std::vector<std::uint8_t>> mFiles;
        PexLoader loader() const
        {
            return [this](const std::string& name, std::vector<std::uint8_t>& bytes) {
                const auto it = mFiles.find(name);
                if (it == mFiles.end())
                    return false;
                bytes = it->second;
                return true;
            };
        }
    };

    QuestScriptIndex indexOf(std::vector<QuestScriptRef> scripts)
    {
        QuestScriptRecords r;
        QuestScriptEntry e;
        e.mQuest = kQuest;
        e.mScripts = std::move(scripts);
        r.mQuests.push_back(std::move(e));
        QuestScriptIndex index;
        index.add(std::move(r));
        return index;
    }

    TEST(SkyrimQuestVariables, anInitialValueOfAnAutoPropertyAndAVariableAreRead)
    {
        using namespace PexAsm;
        Scripts files;
        files.mFiles["myquest"] = ScriptBuilder("MyQuest")
                                      .parent("Quest")
                                      .autoProperty("Counter", "Int", Int(5))
                                      .autoProperty("Ratio", "Float", Flt(1.5f))
                                      .autoProperty("Flag", "Bool", Bool(true))
                                      .var("::Hidden", "float", Flt(2.5f))
                                      .var("::Name", "string", Str("x"))
                                      .build()
                                      .mBytes;
        const QuestScriptIndex index = indexOf({ QuestScriptRef{ "MyQuest", {} } });
        const QuestVariables vars(index, files.loader());
        EXPECT_EQ(vars.value(kQuest, "Counter").mValue, 5.f);
        EXPECT_TRUE(vars.value(kQuest, "COUNTER").mKnown);
        EXPECT_EQ(vars.value(kQuest, "::Counter_var").mValue, 5.f);
        EXPECT_EQ(vars.value(kQuest, "Ratio").mValue, 1.5f);
        EXPECT_EQ(vars.value(kQuest, "Flag").mValue, 1.f);
        EXPECT_EQ(vars.value(kQuest, "::Hidden").mValue, 2.5f);
        EXPECT_FALSE(vars.value(kQuest, "::Name").mKnown); // not a number
        EXPECT_FALSE(vars.value(kQuest, "Missing").mKnown);
        EXPECT_FALSE(vars.value(kQuest + 9, "Counter").mKnown); // no such quest
    }

    TEST(SkyrimQuestVariables, theVmadValueWinsOverTheDeclaredOne)
    {
        using namespace PexAsm;
        Scripts files;
        files.mFiles["myquest"]
            = ScriptBuilder("MyQuest").autoProperty("Counter", "Int", Int(5)).build().mBytes;
        QuestScriptProperty p;
        p.mName = "counter";
        p.mScalar = true;
        p.mValue = 11.f;
        const QuestScriptIndex index = indexOf({ QuestScriptRef{ "MyQuest", { p } } });
        const QuestVariables vars(index, files.loader());
        EXPECT_EQ(vars.value(kQuest, "Counter").mValue, 11.f);
        EXPECT_EQ(vars.value(kQuest, "::Counter_var").mValue, 11.f);
    }

    TEST(SkyrimQuestVariables, aParentScriptsVariablesAreFound)
    {
        using namespace PexAsm;
        Scripts files;
        files.mFiles["child"] = ScriptBuilder("Child").parent("Base").build().mBytes;
        files.mFiles["base"] = ScriptBuilder("Base").autoProperty("Level", "Int", Int(3)).build().mBytes;
        const QuestScriptIndex index = indexOf({ QuestScriptRef{ "Child", {} } });
        const QuestVariables vars(index, files.loader());
        EXPECT_EQ(vars.value(kQuest, "Level").mValue, 3.f);
    }

    TEST(SkyrimQuestVariables, aVariableThatOnInitWritesIsNotAnswered)
    {
        using namespace PexAsm;
        Scripts files;
        files.mFiles["myquest"] = ScriptBuilder("MyQuest")
                                      .autoProperty("Direct", "Int", Int(1))
                                      .autoProperty("ViaCall", "Int", Int(1))
                                      .autoProperty("Untouched", "Int", Int(1))
                                      .var("::plain", "int", Int(4))
                                      .function("OnInit")
                                      .local("tmp", "Int")
                                      .assign(Id("::Direct_var"), Int(9))
                                      .callMethod("Setup", Id("self"), Id("tmp"))
                                      .function("Setup")
                                      .assign(Id("::ViaCall_var"), Int(9))
                                      .function("OnUpdate")
                                      .assign(Id("::Untouched_var"), Int(9))
                                      .build()
                                      .mBytes;
        const QuestScriptIndex index = indexOf({ QuestScriptRef{ "MyQuest", {} } });
        const QuestVariables vars(index, files.loader());
        EXPECT_FALSE(vars.value(kQuest, "Direct").mKnown);
        EXPECT_FALSE(vars.value(kQuest, "ViaCall").mKnown);
        EXPECT_TRUE(vars.value(kQuest, "Untouched").mKnown); // only OnUpdate writes it: no event runs here
        EXPECT_EQ(vars.value(kQuest, "::plain").mValue, 4.f);
    }

    TEST(SkyrimQuestVariables, aMissingOrBrokenScriptAnswersNothing)
    {
        Scripts files;
        files.mFiles["broken"] = { 1, 2, 3 };
        const QuestScriptIndex index
            = indexOf({ QuestScriptRef{ "Absent", {} }, QuestScriptRef{ "Broken", {} } });
        const QuestVariables vars(index, files.loader());
        EXPECT_FALSE(vars.value(kQuest, "Anything").mKnown);
        EXPECT_EQ(vars.scriptsMissing(), 2u);
    }

    TEST(SkyrimConditionState, aQuestVariableIsAnsweredFromTheScriptDefaults)
    {
        using namespace PexAsm;
        Scripts files;
        files.mFiles["myquest"] = ScriptBuilder("MyQuest").autoProperty("Counter", "Int", Int(5)).build().mBytes;
        const QuestScriptIndex index = indexOf({ QuestScriptRef{ "MyQuest", {} } });
        const QuestVariables vars(index, files.loader());
        Fixture f;
        survey::Ctda c = cond(629, kQuest);
        c.mString2 = "Counter";
        c.mComparison = 5.f;
        EXPECT_FALSE(f.mWorld.answers(c)); // no variables given: the old zero, not an answer
        EXPECT_FALSE(f.holds(c));
        f.mWorld.setQuestVariables(&vars);
        EXPECT_TRUE(f.mWorld.answers(c));
        EXPECT_TRUE(f.holds(c));
        c.mComparison = 6.f;
        EXPECT_FALSE(f.holds(c));
        c.mString2 = "Nothing";
        EXPECT_FALSE(f.mWorld.answers(c));
    }
}
