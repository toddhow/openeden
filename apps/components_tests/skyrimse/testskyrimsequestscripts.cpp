// Task 0187, milestone M-S6 "dialogue and quest scripts": INFO script fragments (VMAD parse, dialogue runtime sink, the
// host running a TIF script), quest alias filling, and the story manager (decode, walk, fn_576 GetEventData, the host's
// SendStoryEvent). Generated plugins and scripts; no game data. Written first and run red against stubs.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_alias.hpp>
#include <game_skyrimse/story.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Bool;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kQuestA = kSelf | 0x800;
    constexpr FormId kQuestB = kSelf | 0x801;
    constexpr FormId kQuestC = kSelf | 0x802;
    constexpr FormId kInfo = kSelf | 0x900;
    constexpr FormId kKeyword = kSelf | 0xC01;
    constexpr FormId kLocation = kSelf | 0xD01;
    constexpr FormId kLocation2 = kSelf | 0xD02;
    constexpr FormId kBaseA = kSelf | 0xB00;
    constexpr FormId kBaseB = kSelf | 0xB01;
    constexpr FormId kRef1 = kSelf | 0xA01;
    constexpr FormId kRef2 = kSelf | 0xA02;
    constexpr FormId kRef3 = kSelf | 0xA03;
    constexpr FormId kRef4 = kSelf | 0xA04;

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

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0, std::uint32_t runOn = 0,
        std::uint32_t reference = 0, std::uint8_t flags = 0, std::uint32_t p2 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>((compare << 5) | flags)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(p2).u32(runOn).u32(reference).u32(0);
        return sub4("CTDA", b);
    }

    // ---- VMAD bytes -----------------------------------------------------------------------------------------------

    Bytes wstr(Bytes& b, const std::string& s)
    {
        b.u16(static_cast<std::uint16_t>(s.size()));
        for (char c : s)
            b.u8(static_cast<std::uint8_t>(c));
        return b;
    }

    /// An INFO VMAD: one script `name` with one object property `Prop` (form, no alias), then the fragment section.
    Bytes infoVmad(const std::string& name, std::uint32_t propertyForm, int fragmentFlags, bool section = true,
        const std::string& begin = "Fragment_0", const std::string& end = "Fragment_1")
    {
        Bytes b;
        b.u16(5).u16(2).u16(1);
        wstr(b, name);
        b.u8(1).u16(1);
        wstr(b, "Prop");
        b.u8(1).u8(1).u16(0).u16(0xFFFF).u32(propertyForm);
        if (!section)
            return b;
        b.u8(2).u8(static_cast<std::uint8_t>(fragmentFlags));
        wstr(b, name);
        if ((fragmentFlags & 1) != 0)
        {
            b.u8(1);
            wstr(b, name);
            wstr(b, begin);
        }
        if ((fragmentFlags & 2) != 0)
        {
            b.u8(1);
            wstr(b, name);
            wstr(b, end);
        }
        return b;
    }

    std::vector<std::byte> toBytes(const Bytes& b)
    {
        const std::string s = b.str();
        std::vector<std::byte> out(s.size());
        std::memcpy(out.data(), s.data(), s.size());
        return out;
    }

    // ---- 1. the INFO VMAD ------------------------------------------------------------------------------------------

    TEST(SkyrimInfoVmad, readsTheBeginAndTheEndFragment)
    {
        const std::vector<std::byte> bytes = toBytes(infoVmad("TIF__00000900", kSelf | 0x800, 3));
        const vmad::ParseResult r = vmad::parseInfo(bytes);
        ASSERT_TRUE(r.mOk) << r.mError;
        ASSERT_EQ(r.mVmad.mScripts.size(), 1u);
        EXPECT_EQ(r.mVmad.mScripts[0].mName, "TIF__00000900");
        ASSERT_EQ(r.mVmad.mScripts[0].mProperties.size(), 1u);
        EXPECT_EQ(r.mVmad.mScripts[0].mProperties[0].mValue.mForm, kSelf | 0x800);
        EXPECT_TRUE(r.mVmad.mHasInfoFragments);
        EXPECT_TRUE(r.mVmad.mInfo.hasBegin());
        EXPECT_TRUE(r.mVmad.mInfo.hasEnd());
        EXPECT_EQ(r.mVmad.mInfo.mBeginFunction, "Fragment_0");
        EXPECT_EQ(r.mVmad.mInfo.mEndFunction, "Fragment_1");
        EXPECT_EQ(r.mVmad.mInfo.mBeginScript, "TIF__00000900");
    }

    TEST(SkyrimInfoVmad, anEndOnlyFragmentHasNoBegin)
    {
        const vmad::ParseResult r = vmad::parseInfo(toBytes(infoVmad("TIF__1", 0, 2)));
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_FALSE(r.mVmad.mInfo.hasBegin());
        EXPECT_TRUE(r.mVmad.mInfo.hasEnd());
        EXPECT_EQ(r.mVmad.mInfo.mEndFunction, "Fragment_1");
    }

    TEST(SkyrimInfoVmad, aVmadWithNoFragmentSectionIsAccepted)
    {
        const vmad::ParseResult r = vmad::parseInfo(toBytes(infoVmad("TIF__1", 0, 0, false)));
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_FALSE(r.mVmad.mHasInfoFragments);
        EXPECT_EQ(r.mVmad.mScripts.size(), 1u);
    }

    TEST(SkyrimInfoVmad, anUnknownFlagBitIsRefused)
    {
        const vmad::ParseResult r = vmad::parseInfo(toBytes(infoVmad("TIF__1", 0, 4)));
        EXPECT_FALSE(r.mOk);
        EXPECT_FALSE(r.mError.empty());
    }

    TEST(SkyrimInfoVmad, everyTruncationOfTheFragmentSectionIsRefusedWithAnOffset)
    {
        const Bytes whole = infoVmad("TIF__1", 0, 3);
        const std::string s = whole.str();
        const std::size_t sectionStart = infoVmad("TIF__1", 0, 3, false).str().size();
        for (std::size_t n = sectionStart + 1; n < s.size(); ++n)
        {
            std::vector<std::byte> cut(n);
            std::memcpy(cut.data(), s.data(), n);
            const vmad::ParseResult r = vmad::parseInfo(cut);
            EXPECT_FALSE(r.mOk) << "length " << n;
            EXPECT_NE(r.mError.find("offset"), std::string::npos) << "length " << n;
        }
    }

    TEST(SkyrimInfoVmad, trailingBytesAreRefused)
    {
        Bytes b = infoVmad("TIF__1", 0, 1);
        b.u8(0);
        EXPECT_FALSE(vmad::parseInfo(toBytes(b)).mOk);
    }

    // ---- 2. the dialogue parse and the runtime sink -----------------------------------------------------------------

    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kNpcA = kSelf | 0x200;
    constexpr FormId kRaceNord = 0x00013746;

    Bytes trdt()
    {
        Bytes b;
        b.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        return sub4("TRDT", b);
    }

    Bytes infoRecord(std::uint32_t id, const std::string& text, const Bytes& extra)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        b.append(trdt());
        b.append(sub4("NAM1", Bytes().zstring(text)));
        b.append(sub4("NAM2", Bytes().u8(0)));
        b.append(extra);
        return record4("INFO", 0u, id, b);
    }

    Bytes helloPlugin(const Bytes& infoExtra, const std::string& text = "Hello.")
    {
        Bytes children = infoRecord(kInfo, text, infoExtra);
        Bytes topic;
        topic.append(edid("Hello"));
        topic.append(sub4("PNAM", Bytes().f32(1.0f)));
        topic.append(sub4("QNAM", Bytes().u32(kQuestA)));
        topic.append(sub4("BNAM", Bytes().u32(kSelf | 0x500)));
        topic.append(sub4("DATA", Bytes().u8(0).u8(7).u16(73)));
        topic.append(sub4("SNAM", Bytes().text("HELO")));
        topic.append(sub4("TIFC", Bytes().u32(1)));
        Bytes dial = record4("DIAL", 0, kTopicHello, topic);
        dial.append(group4(kTopicHello, 7, children));
        Bytes npc;
        npc.append(edid("Alice"));
        {
            Bytes acbs;
            acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
            npc.append(sub4("ACBS", acbs));
        }
        npc.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        npc.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        Bytes race;
        race.append(edid("NordRace"));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("DIAL"), 0, dial));
        bytes.append(group4(Content::fourCC("NPC_"), 0, record4("NPC_", 0, kNpcA, npc)));
        bytes.append(group4(Content::fourCC("RACE"), 0, record4("RACE", 0, kRaceNord, race)));
        return bytes;
    }

    struct Talk
    {
        explicit Talk(const Bytes& bytes)
        {
            Content::Error error;
            EXPECT_TRUE(parseDialogueRecordsBytes(bytes.str(), "test.esp", order(), DialogueLimits(), mRecords, error));
            mPlugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            Content::Error e2;
            EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), "test.esp", order(), ActorLimits(), mActorRecords, e2));
            mActors.add(mActorRecords);
            mDialogue.add(mRecords, FormIdResolver(mPlugin->header(), "test.esp", order()));
            mRuntime = std::make_unique<DialogueRuntime>(mActors, mDialogue, mQuests, QuestState::initial(mQuests),
                [](const LString& t, std::uint32_t, StringTableKind) { return t.mText; });
        }
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mRecords;
        ActorRecords mActorRecords;
        ActorIndex mActors;
        DialogueIndex mDialogue;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;
    };

    DialogueSpeaker speaker()
    {
        DialogueSpeaker s;
        s.mBase = kNpcA;
        s.mReference = kSelf | 0x1200;
        return s;
    }

    TEST(SkyrimInfoFragments, anInfoWithFragmentsIsKeptWithItsParsedVmadAndFoundById)
    {
        Talk talk(helloPlugin(sub4("VMAD", infoVmad("TIF__00000900", kQuestA, 3))));
        const DialogInfo* info = talk.mDialogue.scriptedInfo(kInfo);
        ASSERT_NE(info, nullptr);
        ASSERT_NE(info->mVmad, nullptr);
        EXPECT_TRUE(info->mVmad->mInfo.hasBegin());
        // the property's form id is a load-order id now (test.esp is plugin 1)
        EXPECT_EQ(info->mVmad->mScripts.at(0).mProperties.at(0).mValue.mForm, kQuestA);
        EXPECT_EQ(talk.mDialogue.scriptedInfo(kInfo + 1), nullptr);
    }

    TEST(SkyrimInfoFragments, anInfoWithNoVmadIsNotScripted)
    {
        Talk talk(helloPlugin(Bytes()));
        EXPECT_EQ(talk.mDialogue.scriptedInfo(kInfo), nullptr);
    }

    TEST(SkyrimInfoFragments, theSinkGetsTheScriptsOfTheShownGreetingWithTheSpeaker)
    {
        Talk talk(helloPlugin(sub4("VMAD", infoVmad("TIF__00000900", kQuestA, 3))));
        std::vector<InfoFragmentCall> calls;
        talk.mRuntime->setFragmentSink([&calls](const InfoFragmentCall& c) { calls.push_back(c); });
        openeden::sdk::game::DialogueTurn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(speaker(), turn, error)) << error;
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(calls[0].mInfo, kInfo);
        EXPECT_EQ(calls[0].mSpeakerBase, kNpcA);
        EXPECT_EQ(calls[0].mSpeakerReference, kSelf | 0x1200);
        ASSERT_NE(calls[0].mVmad, nullptr);
        EXPECT_EQ(calls[0].mVmad->mInfo.mBeginFunction, "Fragment_0");
        EXPECT_EQ(talk.mRuntime->stats().mFragmentCalls, 1u);
    }

    TEST(SkyrimInfoFragments, noSinkMeansNothingRunsAndTheOldCountsStay)
    {
        Talk talk(helloPlugin(sub4("VMAD", infoVmad("TIF__00000900", kQuestA, 3))));
        openeden::sdk::game::DialogueTurn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(speaker(), turn, error)) << error;
        EXPECT_EQ(talk.mRuntime->stats().mFragmentCalls, 0u);
        EXPECT_EQ(talk.mRuntime->stats().mScriptsNotRun, 1u);
        EXPECT_TRUE(turn.hadUnrunEffects);
    }

    TEST(SkyrimInfoFragments, aVmadWithScriptsButNoFragmentsCallsNoSink)
    {
        Talk talk(helloPlugin(sub4("VMAD", infoVmad("TIF__00000900", kQuestA, 0, false))));
        int calls = 0;
        talk.mRuntime->setFragmentSink([&calls](const InfoFragmentCall&) { ++calls; });
        openeden::sdk::game::DialogueTurn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(speaker(), turn, error)) << error;
        EXPECT_EQ(calls, 0);
    }

    // ---- 3. the host: fake game ------------------------------------------------------------------------------------

    class FakeData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            if (mInfos.count(form) != 0)
                return "TopicInfo";
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        bool isTopicInfo(FormId form) const override { return mInfos.count(form) != 0; }
        FormId questOfInfo(FormId info) const override
        {
            const auto it = mInfoQuests.find(info);
            return it != mInfoQuests.end() ? it->second : 0;
        }
        FormId baseOf(FormId reference) const override
        {
            const auto it = mBases.find(reference);
            return it != mBases.end() ? it->second : 0;
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, FormId> mBases;
        std::set<FormId> mInfos;
        std::map<FormId, FormId> mInfoQuests;
    };

    void addNativeClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder topicInfo("TopicInfo");
        topicInfo.parent("Form");
        topicInfo.function("GetOwningQuest", "Quest").native();
        data.add(topicInfo);
        ScriptBuilder keyword("Keyword");
        keyword.parent("Form");
        keyword.function("SendStoryEvent")
            .native()
            .param("akLoc", "Form")
            .param("akRef1", "Form")
            .param("akRef2", "Form")
            .param("aiValue1", "Int")
            .param("aiValue2", "Int");
        data.add(keyword);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        data.add(actor);
        ScriptBuilder quest("Quest");
        quest.parent("Form");
        quest.function("SetStage", "Bool").native().param("auiStage", "Int");
        quest.function("GetStage", "Int").native();
        quest.function("Start", "Bool").native();
        data.add(quest);
        ScriptBuilder alias("Alias");
        alias.function("GetOwningQuest", "Quest").native();
        data.add(alias);
        ScriptBuilder refAlias("ReferenceAlias");
        refAlias.parent("Alias");
        refAlias.function("GetReference", "ObjectReference").native();
        data.add(refAlias);
        ScriptBuilder location("Location");
        location.parent("Form");
        data.add(location);
    }

    /// TIF script: begin fragment sets the property quest to stage 10 and notes the speaker; end fragment appends to a
    /// trace.
    ScriptBuilder tifScript()
    {
        ScriptBuilder s("TIF__00000900");
        s.parent("TopicInfo");
        s.var("speakerId", "Int");
        s.var("trace", "Int");
        s.autoProperty("Prop", "Quest");
        s.function("Fragment_0")
            .param("akSpeakerRef", "ObjectReference")
            .local("r", "Bool")
            .callMethod("SetStage", Id("::Prop_var"), Id("r"), { Int(10) })
            .local("n", "Int")
            .callMethod("GetFormID", Id("akSpeakerRef"), Id("n"))
            .assign(Id("speakerId"), Id("n"))
            .assign(Id("trace"), Int(1));
        s.function("Fragment_1").param("akSpeakerRef", "ObjectReference").iadd(Id("trace"), Id("trace"), Int(10));
        return s;
    }

    Bytes questRecord(std::uint32_t id, std::uint16_t flags, const Bytes& aliases = Bytes())
    {
        Bytes b;
        b.append(edid("Q" + std::to_string(id & 0xFFF)));
        b.append(sub4("DNAM", Bytes().u16(flags).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        for (std::uint16_t stage : { 10, 20 })
        {
            b.append(sub4("INDX", Bytes().u16(stage).u8(0).u8(0)));
            b.append(sub4("QSDT", Bytes().u8(0)));
        }
        b.append(sub4("ANAM", Bytes().u32(8)));
        b.append(aliases);
        return record4("QUST", 0u, id, b);
    }

    Bytes alias(std::uint32_t id, const std::string& name, std::uint32_t flags, const Bytes& fill,
        const Bytes& conditions = Bytes())
    {
        Bytes b;
        b.append(sub4("ALST", Bytes().u32(id)));
        b.append(sub4("ALID", Bytes().zstring(name)));
        b.append(sub4("FNAM", Bytes().u32(flags)));
        b.append(fill);
        b.append(conditions);
        b.append(sub4("ALED", Bytes()));
        return b;
    }

    struct Rig
    {
        Rig()
        {
            addNativeClasses(mData);
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kQuestB] = "Quest";
            mData.mClasses[kQuestC] = "Quest";
            mData.mClasses[kKeyword] = "Keyword";
            mData.mClasses[kLocation] = "Location";
            mData.mClasses[kRef1] = "Actor";
            mData.mClasses[kRef2] = "ObjectReference";
            mData.mInfos.insert(kInfo);
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
            {
                for (const std::string& l : mLogs)
                    std::cerr << "  log: " << l << std::endl;
                for (const std::string& l : mErrors)
                    std::cerr << "  error: " << l << std::endl;
            }
        }
        void quests(const std::vector<Bytes>& records)
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes all;
            for (const Bytes& r : records)
                all.append(r);
            bytes.append(group4(Content::fourCC("QUST"), 0, all));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [this](FormId q) -> const vmad::Vmad* {
                const auto it = mVmads.find(q);
                return it != mVmads.end() ? &it->second : nullptr;
            });
        }
        void tick(int n = 4)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        FakeData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::map<FormId, vmad::Vmad> mVmads;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    vmad::Vmad tifVmad(int flags)
    {
        const vmad::ParseResult r = vmad::parseInfo(toBytes(infoVmad("TIF__00000900", kQuestA, flags)));
        EXPECT_TRUE(r.mOk) << r.mError;
        return r.mVmad;
    }

    TEST(SkyrimInfoFragments, theHostRunsTheBeginAndThenTheEndFunctionWithTheSpeaker)
    {
        Rig rig;
        rig.mData.add(tifScript());
        rig.quests({ questRecord(kQuestA, 0) });
        const vmad::Vmad v = tifVmad(3);
        EXPECT_EQ(rig.mHost->runInfoFragments(kInfo, v, kRef1, kBaseA), 2u);
        rig.tick();
        EXPECT_TRUE(rig.mState.stageDone(kQuestA, 10)) << "the begin fragment did not set the stage";
        EXPECT_EQ(rig.mHost->numberVariable(kInfo, "TIF__00000900", "speakerId"), std::optional<double>(kRef1));
        // begin ran first (trace = 1), the end added 10: 11
        EXPECT_EQ(rig.mHost->numberVariable(kInfo, "TIF__00000900", "trace"), std::optional<double>(11));
        EXPECT_EQ(rig.mHost->counters().mInfoFragmentsRun, 2u);
    }

    // The real TIF scripts reach their quest through `self.GetOwningQuest()` (TopicInfo): the INFO's quest.
    ScriptBuilder owningQuestScript()
    {
        ScriptBuilder s("TIF__00000900");
        s.parent("TopicInfo");
        s.function("Fragment_0")
            .param("akSpeakerRef", "ObjectReference")
            .local("q", "Quest")
            .callMethod("GetOwningQuest", Id("self"), Id("q"))
            .local("r", "Bool")
            .callMethod("SetStage", Id("q"), Id("r"), { Int(20) });
        return s;
    }

    TEST(SkyrimInfoFragments, theOwningQuestOfTheInfoIsTheQuestTheFragmentCanSetAStageOn)
    {
        Rig rig;
        rig.mData.mInfoQuests[kInfo] = kQuestB;
        rig.mData.add(owningQuestScript());
        rig.quests({ questRecord(kQuestA, 0), questRecord(kQuestB, 0) });
        const vmad::Vmad v = tifVmad(1);
        EXPECT_EQ(rig.mHost->runInfoFragments(kInfo, v, kRef1, kBaseA), 1u);
        rig.tick();
        EXPECT_TRUE(rig.mState.stageDone(kQuestB, 20));
        EXPECT_FALSE(rig.mState.stageDone(kQuestA, 20));
    }

    TEST(SkyrimInfoFragments, aSecondRunReusesTheAttachedScript)
    {
        Rig rig;
        rig.mData.add(tifScript());
        rig.quests({ questRecord(kQuestA, 0) });
        const vmad::Vmad v = tifVmad(1);
        EXPECT_EQ(rig.mHost->runInfoFragments(kInfo, v, kRef1, kBaseA), 1u);
        rig.tick();
        const std::size_t instances = rig.mHost->instanceCount();
        EXPECT_GE(instances, 1u);
        EXPECT_EQ(rig.mHost->runInfoFragments(kInfo, v, kRef1, kBaseA), 1u);
        rig.tick();
        EXPECT_EQ(rig.mHost->instanceCount(), instances);
    }

    TEST(SkyrimInfoFragments, aMissingScriptQueuesNothingAndIsCounted)
    {
        Rig rig;
        rig.quests({ questRecord(kQuestA, 0) });
        const vmad::Vmad v = tifVmad(3);
        EXPECT_EQ(rig.mHost->runInfoFragments(kInfo, v, kRef1, kBaseA), 0u);
        EXPECT_EQ(rig.mHost->counters().mInfoFragmentsRun, 0u);
        EXPECT_EQ(rig.mHost->counters().mScriptsMissing, 1u);
    }

    TEST(SkyrimInfoFragments, theInstanceSurvivesASaveAndALoad)
    {
        Rig rig;
        rig.mData.add(tifScript());
        rig.quests({ questRecord(kQuestA, 0) });
        const vmad::Vmad v = tifVmad(3);
        rig.mHost->startGame();
        (void)rig.mHost->runInfoFragments(kInfo, v, kRef1, kBaseA);
        rig.tick();
        const std::vector<std::uint8_t> saved = rig.mHost->saveState();
        ASSERT_FALSE(saved.empty());
        Rig other;
        other.mData.add(tifScript());
        other.quests({ questRecord(kQuestA, 0) });
        const PapyrusRestoreOutcome outcome = other.mHost->restoreSavedState(saved);
        ASSERT_TRUE(outcome.mOk) << outcome.mError;
        EXPECT_EQ(other.mHost->numberVariable(kInfo, "TIF__00000900", "trace"), std::optional<double>(11));
    }

    // ---- 4. the alias filler ---------------------------------------------------------------------------------------

    PlacedEntry placedRef(FormId id, FormId base, std::uint8_t flags = PlacedFlags::IsActor)
    {
        PlacedEntry r;
        r.mId = id;
        r.mBase = base;
        r.mCell = kSelf | 0x100;
        r.mFlags = flags;
        return r;
    }

    struct FillRig
    {
        FillRig(const std::vector<Bytes>& aliases, std::vector<PlacedEntry> refs)
        {
            Rig builder;
            Bytes joined;
            for (const Bytes& a : aliases)
                joined.append(a);
            builder.quests({ questRecord(kQuestA, 0, joined) });
            mIndex = std::move(builder.mIndex);
            mState = QuestState::initial(mIndex);
            PlacedRefRecords r;
            r.mRefs = std::move(refs);
            mRefs.add(std::move(r));
            mRefs.finish();
            mFiller = std::make_unique<QuestAliasFiller>(
                mIndex, mRefs, [this](const QuestRecord&, const QuestAlias& a, FormId reference, FormId) {
                    mAsked.push_back(reference);
                    return mAccept.count(reference) != 0 && a.mId != 99;
                });
        }
        QuestIndex mIndex;
        QuestState mState;
        PlacedRefIndex mRefs;
        std::set<FormId> mAccept;
        std::vector<FormId> mAsked;
        std::unique_ptr<QuestAliasFiller> mFiller;
    };

    TEST(SkyrimQuestAlias, aUniqueActorGetsTheFirstEnabledReferenceOfItsBase)
    {
        FillRig rig({ alias(3, "Boss", 0, sub4("ALUA", Bytes().u32(kBaseA))) },
            { placedRef(kRef1, kBaseA, PlacedFlags::IsActor | PlacedFlags::Disabled), placedRef(kRef2, kBaseA),
                placedRef(kRef3, kBaseA) });
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, false), 1u);
        const AliasFill fill = rig.mState.aliasFill(kQuestA, 3);
        EXPECT_TRUE(fill.mKnown);
        EXPECT_EQ(fill.mActor, kBaseA);
        EXPECT_EQ(fill.mReference, kRef2);
        EXPECT_EQ(rig.mFiller->stats().mUniqueActorsResolved, 1u);
    }

    TEST(SkyrimQuestAlias, aSpecificReferenceKeepsItsReference)
    {
        FillRig rig({ alias(2, "Thing", 0, sub4("ALFR", Bytes().u32(kRef2))) }, { placedRef(kRef2, kBaseB, 0) });
        (void)rig.mFiller->fillQuest(kQuestA, rig.mState, false);
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 2).mReference, kRef2);
    }

    TEST(SkyrimQuestAlias, aFindMatchingAliasTakesTheFirstReferenceInIdOrderThatPasses)
    {
        FillRig rig({ alias(4, "Any", 0, Bytes(), ctda(72, 0, 1.0f, kBaseA)) },
            { placedRef(kRef1, kBaseA), placedRef(kRef2, kBaseA), placedRef(kRef3, kBaseA), placedRef(kRef4, kBaseB) });
        rig.mAccept = { kRef2, kRef3 };
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 4).mReference, kRef2);
        // the GetIsID narrowed the candidates to the base: the reference of base B was never asked
        EXPECT_EQ(std::count(rig.mAsked.begin(), rig.mAsked.end(), kRef4), 0);
        EXPECT_EQ(rig.mAsked, (std::vector<FormId>{ kRef1, kRef2 }));
        EXPECT_EQ(rig.mFiller->stats().mMatched, 1u);
    }

    TEST(SkyrimQuestAlias, withoutMatchingTheConditionalAliasesStayEmptyUntilAskedFor)
    {
        FillRig rig({ alias(4, "Any", 0, Bytes(), ctda(72, 0, 1.0f, kBaseA)) }, { placedRef(kRef1, kBaseA) });
        rig.mAccept = { kRef1 };
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, false), 0u);
        EXPECT_TRUE(rig.mAsked.empty());
        EXPECT_TRUE(rig.mFiller->fillAlias(kQuestA, 4, rig.mState));
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 4).mReference, kRef1);
    }

    TEST(SkyrimQuestAlias, aDisabledOrDeadCandidateIsSkippedUnlessTheAliasAllowsIt)
    {
        const Bytes cond = ctda(72, 0, 1.0f, kBaseA);
        FillRig rig({ alias(4, "Any", 0, Bytes(), cond),
                        alias(5, "Dis", AliasFlags::AllowDisabled | AliasFlags::AllowReuseInQuest, Bytes(), cond) },
            { placedRef(kRef1, kBaseA, PlacedFlags::IsActor | PlacedFlags::Disabled),
                placedRef(kRef2, kBaseA, PlacedFlags::IsActor | PlacedFlags::Dead), placedRef(kRef3, kBaseA) });
        rig.mAccept = { kRef1, kRef2, kRef3 };
        (void)rig.mFiller->fillQuest(kQuestA, rig.mState, true);
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 4).mReference, kRef3);
        EXPECT_EQ(
            rig.mState.aliasFill(kQuestA, 5).mReference, kRef1); // the disabled one is allowed; the dead one is not
    }

    TEST(SkyrimQuestAlias, aReferenceAlreadyInAnotherAliasIsNotTakenAgainUnlessReuseIsAllowed)
    {
        const Bytes cond = ctda(72, 0, 1.0f, kBaseA);
        FillRig rig({ alias(4, "First", 0, Bytes(), cond), alias(5, "Second", 0, Bytes(), cond),
                        alias(6, "Third", AliasFlags::AllowReuseInQuest, Bytes(), cond) },
            { placedRef(kRef1, kBaseA), placedRef(kRef2, kBaseA) });
        rig.mAccept = { kRef1, kRef2 };
        (void)rig.mFiller->fillQuest(kQuestA, rig.mState, true);
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 4).mReference, kRef1);
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 5).mReference, kRef2);
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 6).mReference, kRef1);
    }

    TEST(SkyrimQuestAlias, anAliasNothingPassesForStaysEmptyAndIsCounted)
    {
        FillRig rig({ alias(99, "None", 0, Bytes(), ctda(72, 0, 1.0f, kBaseA)) }, { placedRef(kRef1, kBaseA) });
        rig.mAccept = { kRef1 };
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 0u);
        EXPECT_FALSE(rig.mState.aliasFill(kQuestA, 99).mKnown && rig.mState.aliasFill(kQuestA, 99).mReference != 0);
        EXPECT_EQ(rig.mFiller->stats().mUnmatched, 1u);
    }

    TEST(SkyrimQuestAlias, aFilledAliasIsNotFilledAgain)
    {
        FillRig rig({ alias(3, "Boss", 0, sub4("ALUA", Bytes().u32(kBaseA))) }, { placedRef(kRef2, kBaseA) });
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, false), 1u);
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, false), 0u);
    }

    // ---- 5. aliases through the host -------------------------------------------------------------------------------

    ScriptBuilder aliasReader()
    {
        ScriptBuilder s("AliasReader");
        s.parent("Quest");
        s.var("seen", "Int");
        s.autoProperty("TheAlias", "ReferenceAlias");
        s.function("Read")
            .local("r", "ObjectReference")
            .callMethod("GetReference", Id("::TheAlias_var"), Id("r"))
            .local("n", "Int")
            .callMethod("GetFormID", Id("r"), Id("n"))
            .assign(Id("seen"), Id("n"));
        return s;
    }

    vmad::Vmad readerVmad(FormId quest, std::int16_t aliasId)
    {
        vmad::Vmad v;
        vmad::Script s;
        s.mName = "AliasReader";
        vmad::Property p;
        p.mName = "TheAlias";
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = quest;
        p.mValue.mAlias = aliasId;
        s.mProperties.push_back(p);
        v.mScripts.push_back(s);
        return v;
    }

    ScriptBuilder starter()
    {
        ScriptBuilder s("Starter");
        s.parent("Quest");
        s.autoProperty("Target", "Quest");
        s.function("Go").local("r", "Bool").callMethod("Start", Id("::Target_var"), Id("r"));
        return s;
    }

    // A native cannot load a script, so what a stage change needs loaded (the target quest's fragment script) is done
    // right after the calling script returned.
    ScriptBuilder fragmentOfB()
    {
        ScriptBuilder s("QF_B");
        s.parent("Quest");
        s.var("ran", "Int");
        s.function("Fragment_3").assign(Id("ran"), Int(1));
        return s;
    }

    ScriptBuilder stageSetter()
    {
        ScriptBuilder s("StageSetter");
        s.parent("Quest");
        s.autoProperty("Target", "Quest");
        s.function("Go").local("r", "Bool").callMethod("SetStage", Id("::Target_var"), Id("r"), { Int(10) });
        return s;
    }

    TEST(SkyrimQuestAlias, aStageSetByAScriptOnAQuestWhoseScriptsAreNotLoadedAttachesThemAndRunsTheFragment)
    {
        Rig rig;
        rig.mData.add(stageSetter());
        rig.mData.add(fragmentOfB());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled), questRecord(kQuestB, 0) });
        rig.mVmads[kQuestA] = [] {
            vmad::Vmad v;
            vmad::Script s;
            s.mName = "StageSetter";
            vmad::Property p;
            p.mName = "Target";
            p.mType = 1;
            p.mValue.mKind = vmad::ValueKind::Object;
            p.mValue.mForm = kQuestB;
            s.mProperties.push_back(p);
            v.mScripts.push_back(s);
            return v;
        }();
        vmad::Vmad b;
        b.mHasFragments = true;
        b.mFragmentScript = "QF_B";
        b.mFragments.push_back({ 10, 1, "QF_B", "Fragment_3" });
        vmad::Script fragmentScript;
        fragmentScript.mName = "QF_B";
        b.mScripts.push_back(fragmentScript);
        rig.mVmads[kQuestB] = b;
        rig.mHost->startGame();
        EXPECT_TRUE(rig.mHost->callFunction(kQuestA, "StageSetter", "Go"));
        rig.tick();
        EXPECT_TRUE(rig.mState.stageDone(kQuestB, 10));
        EXPECT_EQ(rig.mHost->numberVariable(kQuestB, "QF_B", "ran"), std::optional<double>(1));
        EXPECT_EQ(rig.mHost->counters().mFragmentsRun, 1u);
    }

    TEST(SkyrimQuestAlias, aScriptSeesTheReferenceOfAUniqueActorAliasOnceTheQuestHasStarted)
    {
        Rig rig;
        rig.mData.add(aliasReader());
        rig.mData.add(starter());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled),
            questRecord(kQuestB, 0, alias(3, "Boss", 0, sub4("ALUA", Bytes().u32(kBaseA)))) });
        rig.mVmads[kQuestB] = readerVmad(kQuestB, 3);
        vmad::Vmad starterVmad;
        vmad::Script starterScript;
        starterScript.mName = "Starter";
        vmad::Property target;
        target.mName = "Target";
        target.mType = 1;
        target.mValue.mKind = vmad::ValueKind::Object;
        target.mValue.mForm = kQuestB;
        starterScript.mProperties.push_back(target);
        starterVmad.mScripts.push_back(starterScript);
        rig.mVmads[kQuestA] = starterVmad;
        PlacedRefIndex refs;
        PlacedRefRecords records;
        records.mRefs = { placedRef(kRef1, kBaseA) };
        refs.add(std::move(records));
        refs.finish();
        QuestAliasFiller filler(
            rig.mIndex, refs, [](const QuestRecord&, const QuestAlias&, FormId, FormId) { return true; });
        rig.mHost->setAliasFiller(&filler);
        rig.mHost->startGame();
        EXPECT_FALSE(rig.mState.running(kQuestB));
        EXPECT_TRUE(rig.mHost->callFunction(kQuestA, "Starter", "Go"));
        EXPECT_TRUE(rig.mState.running(kQuestB));
        EXPECT_TRUE(rig.mHost->callFunction(kQuestB, "AliasReader", "Read"));
        EXPECT_EQ(rig.mHost->numberVariable(kQuestB, "AliasReader", "seen"), std::optional<double>(kRef1));
        EXPECT_GE(rig.mHost->counters().mAliasesFilled, 1u);
    }

    TEST(SkyrimQuestAlias, anAliasNobodyFilledAtStartIsFilledWhenAScriptAsksForIt)
    {
        Rig rig;
        rig.mData.add(aliasReader());
        rig.quests({ questRecord(
            kQuestB, QuestFlags::StartGameEnabled, alias(3, "Any", 0, Bytes(), ctda(72, 0, 1.0f, kBaseA))) });
        rig.mVmads[kQuestB] = readerVmad(kQuestB, 3);
        PlacedRefIndex refs;
        PlacedRefRecords records;
        records.mRefs = { placedRef(kRef2, kBaseA), placedRef(kRef1, kBaseA) };
        refs.add(std::move(records));
        refs.finish();
        QuestAliasFiller filler(
            rig.mIndex, refs, [](const QuestRecord&, const QuestAlias&, FormId, FormId) { return true; });
        rig.mHost->setAliasFiller(&filler);
        rig.mHost->startGame();
        EXPECT_TRUE(rig.mHost->callFunction(kQuestB, "AliasReader", "Read"));
        // candidates are taken in form id order: kRef1 (0xA01) comes before kRef2 (0xA02)
        EXPECT_EQ(rig.mHost->numberVariable(kQuestB, "AliasReader", "seen"), std::optional<double>(kRef1));
    }

    // ---- 6. the story manager --------------------------------------------------------------------------------------

    constexpr FormId kRoot = kSelf | 0x1000;
    constexpr FormId kEvent = kSelf | 0x1001;
    constexpr FormId kBranch = kSelf | 0x1002;
    constexpr FormId kNodeFirst = kSelf | 0x1003;
    constexpr FormId kNodeSecond = kSelf | 0x1004;
    constexpr FormId kNodeThird = kSelf | 0x1005;

    Bytes nodeCommon(const std::string& name, FormId parent, FormId sibling, const Bytes& conditions,
        std::uint32_t dnam, std::uint32_t conditionCount)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("PNAM", Bytes().u32(parent)));
        b.append(sub4("SNAM", Bytes().u32(sibling)));
        b.append(sub4("CITC", Bytes().u32(conditionCount)));
        b.append(conditions);
        b.append(sub4("DNAM", Bytes().u32(dnam)));
        b.append(sub4("XNAM", Bytes().u32(0)));
        return b;
    }

    Bytes smen(FormId id, FormId parent, const std::string& event)
    {
        Bytes b = nodeCommon("Ev" + std::to_string(id & 0xFFF), parent, 0, Bytes(), 0, 0);
        b.append(sub4("ENAM", Bytes().text(event)));
        return record4("SMEN", 0u, id, b);
    }

    Bytes smbn(FormId id, FormId parent, FormId sibling, std::uint32_t dnam = 0, const Bytes& conds = Bytes(),
        std::uint32_t nconds = 0)
    {
        return record4(
            "SMBN", 0u, id, nodeCommon("Br" + std::to_string(id & 0xFFF), parent, sibling, conds, dnam, nconds));
    }

    Bytes smqn(FormId id, FormId parent, FormId sibling, const std::vector<FormId>& quests, std::uint32_t dnam = 0,
        const Bytes& conds = Bytes(), std::uint32_t nconds = 0, std::uint32_t mnam = 0)
    {
        Bytes b = nodeCommon("Qn" + std::to_string(id & 0xFFF), parent, sibling, conds, dnam, nconds);
        if (mnam != 0)
            b.append(sub4("MNAM", Bytes().u32(mnam)));
        b.append(sub4("QNAM", Bytes().u32(static_cast<std::uint32_t>(quests.size()))));
        for (FormId q : quests)
            b.append(sub4("NNAM", Bytes().u32(q)));
        return record4("SMQN", 0u, id, b);
    }

    struct StoryRig
    {
        explicit StoryRig(const std::vector<Bytes>& nodeRecords, const std::vector<Bytes>& questRecords = {})
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes all;
            for (const Bytes& r : nodeRecords)
                all.append(r);
            bytes.append(group4(Content::fourCC("SMQN"), 0, all));
            StoryRecords parsed;
            Content::Error error;
            mOk = parseStoryRecordsBytes(bytes.str(), "test.esp", order(), StoryLimits(), parsed, error);
            mParsed = parsed.mNodes.size();
            mOdd = parsed.mOddSubrecords;
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mNodes.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            Rig builder;
            builder.quests(questRecords.empty()
                    ? std::vector<Bytes>{ questRecord(kQuestA, 0), questRecord(kQuestB, 0), questRecord(kQuestC, 0) }
                    : questRecords);
            mQuests = std::move(builder.mIndex);
            mState = QuestState::initial(mQuests);
            mManager = std::make_unique<StoryManager>(mNodes, mQuests);
        }
        std::vector<FormId> raise(const std::string& type, bool pass = true)
        {
            StoryEvent e;
            e.mType = type;
            return mManager->raise(
                e,
                [pass, this](const std::vector<survey::Ctda>& l, const FormIdResolver&) {
                    mEvaluated.push_back(l.size());
                    return pass;
                },
                [this](FormId q) { return !mState.running(q) && !mState.completed(q); });
        }
        bool mOk = false;
        std::size_t mParsed = 0;
        std::uint64_t mOdd = 0;
        StoryIndex mNodes;
        QuestIndex mQuests;
        QuestState mState;
        std::unique_ptr<StoryManager> mManager;
        std::vector<std::size_t> mEvaluated;
    };

    TEST(SkyrimStory, nodesAreDecodedAndTheChildrenFollowThePreviousSiblingChain)
    {
        // SNAM names the PREVIOUS sibling: Third -> Second -> First
        StoryRig rig(
            { smbn(kRoot, 0, 0), smen(kEvent, kRoot, "SCPT"), smqn(kNodeThird, kEvent, kNodeSecond, { kQuestC }),
                smqn(kNodeFirst, kEvent, 0, { kQuestA }), smqn(kNodeSecond, kEvent, kNodeFirst, { kQuestB }) });
        ASSERT_TRUE(rig.mOk);
        EXPECT_EQ(rig.mParsed, 5u);
        EXPECT_EQ(rig.mOdd, 0u);
        ASSERT_NE(rig.mNodes.node(kNodeFirst), nullptr);
        EXPECT_EQ(rig.mNodes.node(kNodeFirst)->mKind, StoryNodeKind::Quest);
        EXPECT_EQ(rig.mNodes.children(kEvent), (std::vector<FormId>{ kNodeFirst, kNodeSecond, kNodeThird }));
        EXPECT_EQ(rig.mNodes.eventNodes("SCPT"), (std::vector<FormId>{ kEvent }));
        EXPECT_TRUE(rig.mNodes.eventNodes("CLOC").empty());
        EXPECT_EQ(rig.mNodes.node(kEvent)->mEvent, "SCPT");
    }

    TEST(SkyrimStory, aBrokenSiblingChainFallsBackToFormIdOrder)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"), smqn(kNodeSecond, kEvent, kNodeThird, { kQuestB }),
            smqn(kNodeFirst, kEvent, kNodeSecond, { kQuestA }), smqn(kNodeThird, kEvent, kNodeFirst, { kQuestC }) });
        EXPECT_EQ(rig.mNodes.children(kEvent), (std::vector<FormId>{ kNodeFirst, kNodeSecond, kNodeThird }));
    }

    TEST(SkyrimStory, theQuestNodeStoresItsQuestsAndANumberToRun)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"),
            smqn(kNodeFirst, kEvent, 0, { kQuestA, kQuestB }, StoryFlags::NumQuestsToRun, Bytes(), 0, 2) });
        const StoryNode* n = rig.mNodes.node(kNodeFirst);
        ASSERT_NE(n, nullptr);
        EXPECT_EQ(n->mQuests, (std::vector<FormId>{ kQuestA, kQuestB }));
        EXPECT_EQ(n->mQuestsToRun, 2u);
    }

    TEST(SkyrimStory, anEventWalkStartsTheFirstQuestOfTheFirstNodeAndStops)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"), smqn(kNodeFirst, kEvent, 0, { kQuestA, kQuestB }),
            smqn(kNodeSecond, kEvent, kNodeFirst, { kQuestC }) });
        EXPECT_EQ(rig.raise("SCPT"), (std::vector<FormId>{ kQuestA }));
        EXPECT_EQ(rig.mManager->stats().mQuestsStarted, 1u);
    }

    TEST(SkyrimStory, aNodeThatSharesTheEventLetsTheWalkGoOn)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"), smqn(kNodeFirst, kEvent, 0, { kQuestA }, StoryFlags::SharesEvent),
            smqn(kNodeSecond, kEvent, kNodeFirst, { kQuestC }) });
        EXPECT_EQ(rig.raise("SCPT"), (std::vector<FormId>{ kQuestA, kQuestC }));
    }

    TEST(SkyrimStory, aNodeWhoseConditionsFailIsSkippedWithItsChildren)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"), smbn(kBranch, kEvent, 0, 0, ctda(576, 0, 1.0f), 1),
            smqn(kNodeFirst, kBranch, 0, { kQuestA }), smqn(kNodeSecond, kEvent, kBranch, { kQuestB }) });
        // every condition list fails: the branch is skipped, the second node has no conditions (an empty list passes)
        StoryEvent e;
        e.mType = "SCPT";
        const std::vector<FormId> started = rig.mManager->raise(
            e, [](const std::vector<survey::Ctda>& l, const FormIdResolver&) { return l.empty(); },
            [&rig](FormId q) { return !rig.mState.running(q); });
        EXPECT_EQ(started, (std::vector<FormId>{ kQuestB }));
    }

    TEST(SkyrimStory, aQuestThatIsRunningOrWhoseStartConditionsFailIsSkipped)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"), smqn(kNodeFirst, kEvent, 0, { kQuestA, kQuestB, kQuestC }) });
        rig.mState.start(kQuestA);
        EXPECT_EQ(rig.raise("SCPT"), (std::vector<FormId>{ kQuestB }));
        EXPECT_GE(rig.mManager->stats().mQuestsSkipped, 1u);
    }

    TEST(SkyrimStory, theNumberOfQuestsToRunIsHonoured)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"),
            smqn(kNodeFirst, kEvent, 0, { kQuestA, kQuestB, kQuestC }, StoryFlags::NumQuestsToRun, Bytes(), 0, 2) });
        EXPECT_EQ(rig.raise("SCPT"), (std::vector<FormId>{ kQuestA, kQuestB }));
    }

    TEST(SkyrimStory, doAllBeforeRepeatingRotatesThroughTheQuests)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"),
            smqn(kNodeFirst, kEvent, 0, { kQuestA, kQuestB }, StoryFlags::DoAllBeforeRepeating) });
        const std::vector<FormId> first = rig.raise("SCPT");
        ASSERT_EQ(first.size(), 1u);
        EXPECT_EQ(first[0], kQuestA);
        // the caller did not start it (still startable): the node must go on to the other quest, not repeat A
        const std::vector<FormId> second = rig.raise("SCPT");
        ASSERT_EQ(second.size(), 1u);
        EXPECT_EQ(second[0], kQuestB);
    }

    TEST(SkyrimStory, anEventTypeWithNoNodeStartsNothingAndIsCounted)
    {
        StoryRig rig({ smen(kEvent, kRoot, "SCPT"), smqn(kNodeFirst, kEvent, 0, { kQuestA }) });
        EXPECT_TRUE(rig.raise("CLOC").empty());
        EXPECT_EQ(rig.mManager->stats().mEventsWithNoNode, 1u);
    }

    TEST(SkyrimStory, aRandomBranchWalksItsChildrenInAnOrderThatDependsOnlyOnTheEventNumber)
    {
        const std::vector<Bytes> records = { smen(kEvent, kRoot, "SCPT"), smbn(kBranch, kEvent, 0, StoryFlags::Random),
            smqn(kNodeFirst, kBranch, 0, { kQuestA }), smqn(kNodeSecond, kBranch, kNodeFirst, { kQuestB }),
            smqn(kNodeThird, kBranch, kNodeSecond, { kQuestC }) };
        StoryRig a(records);
        StoryRig b(records);
        std::vector<FormId> ra, rb;
        for (int i = 0; i < 6; ++i)
        {
            const std::vector<FormId> x = a.raise("SCPT");
            const std::vector<FormId> y = b.raise("SCPT");
            ra.insert(ra.end(), x.begin(), x.end());
            rb.insert(rb.end(), y.begin(), y.end());
        }
        EXPECT_EQ(ra, rb);
        EXPECT_EQ(ra.size(), 6u);
        EXPECT_GT(std::set<FormId>(ra.begin(), ra.end()).size(), 1u); // not always the same first child
    }

    // ---- fn_576 GetEventData ---------------------------------------------------------------------------------------

    class EventWorld final : public conditions::ConditionWorld, public StoryEventSource
    {
    public:
        conditions::Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return 1; }
        bool hasBaseId(conditions::Subject, std::uint32_t) const override { return false; }
        float questStage(std::uint32_t) const override { return 0; }
        bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
        bool inFaction(conditions::Subject, std::uint32_t) const override { return false; }
        float globalValue(std::uint32_t) const override { return 0; }
        const StoryEvent* currentStoryEvent() const override { return &mEvent; }
        FormId resolveStoryForm(std::uint32_t stored) const override { return stored; }
        bool storyLocationWithin(FormId l, FormId a) const override
        {
            return l == a || (l == kLocation2 && a == kLocation);
        }
        bool storyLocationHasKeyword(FormId l, FormId k) const override { return l == kLocation && k == kKeyword; }
        FormId storyBaseOf(FormId r) const override { return r == kRef1 ? kBaseA : 0; }
        StoryEvent mEvent;
    };

    // param 1: the member characters in the high half, the function in the low half
    std::uint32_t member(char a, char b, std::uint16_t function)
    {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 24)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 16) | function;
    }

    bool passes(EventWorld& w, std::uint32_t p1, std::uint32_t p2, std::uint8_t compare, float value)
    {
        conditions::Evaluator evaluator;
        evaluator.setExtraFunctions(storyExtraFunctions());
        survey::Ctda c;
        c.mFunction = 576;
        c.mParam1 = p1;
        c.mParam2 = p2;
        c.mCompare = compare;
        c.mComparison = value;
        return evaluator.evaluate(std::vector<survey::Ctda>{ c }, w);
    }

    TEST(SkyrimStoryEventData, theKeywordMemberIsComparedWithTheFormOfTheCondition)
    {
        EventWorld w;
        w.mEvent.mKeyword = kKeyword;
        EXPECT_TRUE(passes(w, member('K', '1', 0), kKeyword, 0, 1.0f));
        EXPECT_FALSE(passes(w, member('K', '1', 0), kKeyword ^ 0x10, 0, 1.0f));
    }

    TEST(SkyrimStoryEventData, theNewLocationMemberMatchesALocationInsideTheNamedOne)
    {
        EventWorld w;
        w.mEvent.mLocation2 = kLocation2;
        EXPECT_TRUE(passes(w, member('L', '2', 0), kLocation, 0, 1.0f));
        EXPECT_FALSE(passes(w, member('L', '1', 0), kLocation, 0, 1.0f));
    }

    TEST(SkyrimStoryEventData, aLocationMemberCanBeAskedForAKeyword)
    {
        EventWorld w;
        w.mEvent.mLocation1 = kLocation;
        EXPECT_TRUE(passes(w, member('L', '1', 3), kKeyword, 0, 1.0f));
        EXPECT_FALSE(passes(w, member('L', '1', 3), kKeyword ^ 0x10, 0, 1.0f));
    }

    TEST(SkyrimStoryEventData, theNumberMembersAreReadAsNumbers)
    {
        EventWorld w;
        w.mEvent.mValue1 = 7;
        w.mEvent.mValue2 = 2;
        EXPECT_TRUE(passes(w, member('V', '1', 2), 0, 0, 7.0f));
        EXPECT_TRUE(passes(w, member('V', '2', 2), 0, 0, 2.0f));
        EXPECT_FALSE(passes(w, member('V', '1', 2), 0, 0, 2.0f));
    }

    TEST(SkyrimStoryEventData, aReferenceMemberMatchesTheReferenceOrItsBase)
    {
        EventWorld w;
        w.mEvent.mRef1 = kRef1;
        EXPECT_TRUE(passes(w, member('R', '1', 0), kRef1, 0, 1.0f));
        EXPECT_TRUE(passes(w, member('R', '1', 0), kBaseA, 0, 1.0f));
        EXPECT_FALSE(passes(w, member('R', '2', 0), kBaseA, 0, 1.0f));
    }

    // ---- 7. SendStoryEvent through the host
    // --------------------------------------------------------------------------

    ScriptBuilder sender()
    {
        ScriptBuilder s("Sender");
        s.parent("Quest");
        s.autoProperty("Kw", "Keyword");
        s.autoProperty("Where", "Location");
        s.autoProperty("Who", "ObjectReference");
        s.function("Send")
            .local("none", "None")
            .callMethod("SendStoryEvent", Id("::Kw_var"), Id("none"),
                { Id("::Where_var"), Id("::Who_var"), Id("::Who_var"), Int(5), Int(6) });
        return s;
    }

    TEST(SkyrimStoryHost, sendStoryEventPassesItsArgumentsToTheSinkAndStartsTheReturnedQuests)
    {
        Rig rig;
        rig.mData.add(sender());
        rig.quests({ questRecord(kQuestA, QuestFlags::StartGameEnabled), questRecord(kQuestB, 0) });
        vmad::Vmad v;
        vmad::Script s;
        s.mName = "Sender";
        for (auto [name, form] :
            { std::pair<const char*, FormId>{ "Kw", kKeyword }, { "Where", kLocation }, { "Who", kRef2 } })
        {
            vmad::Property p;
            p.mName = name;
            p.mType = 1;
            p.mValue.mKind = vmad::ValueKind::Object;
            p.mValue.mForm = form;
            s.mProperties.push_back(p);
        }
        v.mScripts.push_back(s);
        rig.mVmads[kQuestA] = v;
        StoryEvent seen;
        int calls = 0;
        rig.mHost->setStoryEventSink([&](const StoryEvent& e) {
            seen = e;
            ++calls;
            return std::vector<FormId>{ kQuestB };
        });
        rig.mHost->startGame();
        EXPECT_TRUE(rig.mHost->callFunction(kQuestA, "Sender", "Send"));
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(seen.mType, "SCPT");
        EXPECT_EQ(seen.mKeyword, kKeyword);
        EXPECT_EQ(seen.mLocation1, kLocation);
        EXPECT_EQ(seen.mRef1, kRef2);
        EXPECT_EQ(seen.mValue1, 5);
        EXPECT_EQ(seen.mValue2, 6);
        EXPECT_TRUE(rig.mState.running(kQuestB));
        EXPECT_EQ(rig.mHost->counters().mStoryEvents, 1u);
        EXPECT_EQ(rig.mHost->counters().mStoryQuestsStarted, 1u);
    }

    TEST(SkyrimStoryHost, raiseStoryEventStartsTheQuestsTheSinkReturnsAndOnlyOnce)
    {
        Rig rig;
        rig.quests({ questRecord(kQuestA, 0) });
        rig.mHost->setStoryEventSink([](const StoryEvent&) { return std::vector<FormId>{ kQuestA, kQuestA }; });
        StoryEvent e;
        e.mType = "CLOC";
        EXPECT_EQ(rig.mHost->raiseStoryEvent(e), 1u);
        EXPECT_TRUE(rig.mState.running(kQuestA));
    }

    TEST(SkyrimStoryHost, withoutASinkAnEventStartsNothing)
    {
        Rig rig;
        rig.quests({ questRecord(kQuestA, 0) });
        EXPECT_EQ(rig.mHost->raiseStoryEvent(StoryEvent{}), 0u);
    }
}
