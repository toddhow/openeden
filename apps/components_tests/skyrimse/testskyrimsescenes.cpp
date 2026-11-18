// Task 0194, milestone M-S6 "scenes and more story events": the SCEN decoder, the scene fragment VMAD and the scene
// runner (pure, behind a fake environment). Generated records; no game data. Written first and run red against stubs.
#include <game_skyrimse/scene.hpp>
#include <game_skyrimse/scene_runner.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kScene = kSelf | 0x900;
    constexpr FormId kScene2 = kSelf | 0x901;
    constexpr FormId kQuest = kSelf | 0x800;
    constexpr FormId kTopicA = kSelf | 0xA01;
    constexpr FormId kTopicB = kSelf | 0xA02;
    constexpr FormId kPackage = kSelf | 0xB01;
    constexpr FormId kPackage2 = kSelf | 0xB02;
    constexpr FormId kRefHulda = kSelf | 0xC01;
    constexpr FormId kRefMikael = kSelf | 0xC02;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes empty(const char (&type)[5])
    {
        return sub4(type, Bytes());
    }

    Bytes ctda(std::uint16_t function, float value, std::uint32_t p1 = 0, std::uint32_t p2 = 0)
    {
        Bytes b;
        b.u8(0x00).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(p2).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    Bytes phase(const Bytes& startConditions = Bytes(), const Bytes& completionConditions = Bytes(),
        const std::string& name = "")
    {
        Bytes b;
        b.append(empty("HNAM"));
        b.append(sub4("NAM0", Bytes().zstring(name)));
        b.append(startConditions);
        b.append(empty("NEXT"));
        b.append(completionConditions);
        b.append(empty("NEXT"));
        b.append(sub4("WNAM", Bytes().u32(200)));
        b.append(empty("HNAM"));
        return b;
    }

    Bytes actor(std::uint32_t alias)
    {
        Bytes b;
        b.append(sub4("ALID", Bytes().u32(alias)));
        b.append(sub4("LNAM", Bytes().u32(0)));
        b.append(sub4("DNAM", Bytes().u32(26)));
        return b;
    }

    Bytes actionHead(
        std::uint16_t type, std::uint32_t alias, std::uint32_t index, std::uint32_t start, std::uint32_t end)
    {
        Bytes b;
        b.append(sub4("ANAM", Bytes().u16(type)));
        b.append(sub4("NAM0", Bytes().zstring("")));
        b.append(sub4("ALID", Bytes().u32(alias)));
        b.append(sub4("INAM", Bytes().u32(index)));
        b.append(sub4("SNAM", Bytes().u32(start)));
        b.append(sub4("ENAM", Bytes().u32(end)));
        return b;
    }

    Bytes dialogueAction(std::uint32_t alias, std::uint32_t index, std::uint32_t phase, FormId topic)
    {
        Bytes b = actionHead(0, alias, index, phase, phase);
        b.append(sub4("DATA", Bytes().u32(topic)));
        b.append(sub4("HTID", Bytes().u32(0xFFFFFFFFu)));
        b.append(sub4("DMAX", Bytes().f32(10.f)));
        b.append(sub4("DMIN", Bytes().f32(1.f)));
        b.append(sub4("DEMO", Bytes().u32(0)));
        b.append(sub4("DEVA", Bytes().u32(0)));
        b.append(empty("ANAM"));
        return b;
    }

    Bytes packageAction(std::uint32_t alias, std::uint32_t index, std::uint32_t start, std::uint32_t end,
        const std::vector<FormId>& packages)
    {
        Bytes b = actionHead(1, alias, index, start, end);
        for (FormId p : packages)
            b.append(sub4("PNAM", Bytes().u32(p)));
        b.append(empty("ANAM"));
        return b;
    }

    Bytes timerAction(std::uint32_t index, std::uint32_t start, std::uint32_t end, float seconds)
    {
        Bytes b = actionHead(2, 0, index, start, end);
        b.append(sub4("SNAM", Bytes().f32(seconds)));
        b.append(empty("ANAM"));
        return b;
    }

    Bytes scenRecord(FormId id, std::uint32_t flags, const std::vector<Bytes>& phases,
        const std::vector<std::uint32_t>& actors, const std::vector<Bytes>& actions, FormId quest,
        const Bytes& sceneConditions = Bytes(), const Bytes& vmad = Bytes())
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("Scene" + std::to_string(id & 0xFFF))));
        if (!vmad.str().empty())
            b.append(sub4("VMAD", vmad));
        b.append(sub4("FNAM", Bytes().u32(flags)));
        for (const Bytes& p : phases)
            b.append(p);
        for (std::uint32_t a : actors)
            b.append(actor(a));
        for (const Bytes& a : actions)
            b.append(a);
        b.append(sub4("PNAM", Bytes().u32(quest)));
        b.append(sub4("INAM", Bytes().u32(static_cast<std::uint32_t>(actions.size()))));
        b.append(sub4("VNAM", Bytes().u32(3).u32(3).u32(3).u32(3)));
        b.append(sceneConditions);
        return record4("SCEN", 0u, id, b);
    }

    struct Parsed
    {
        explicit Parsed(const std::vector<Bytes>& records)
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes all;
            for (const Bytes& r : records)
                all.append(r);
            bytes.append(group4(Content::fourCC("SCEN"), 0, all));
            Content::Error error;
            mOk = parseSceneRecordsBytes(bytes.str(), "test.esp", order(), SceneLimits(), mRecords, error);
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mCount = mRecords.mScenes.size();
            if (plugin != nullptr)
                mIndex.add(SceneRecords(mRecords), FormIdResolver(plugin->header(), "test.esp", order()));
        }
        bool mOk = false;
        std::size_t mCount = 0;
        SceneRecords mRecords;
        SceneIndex mIndex;
    };

    // ---- decoding -------------------------------------------------------------------------------------------------

    TEST(SceneDecode, aSceneWithAllThreeActionKindsReadsEveryField)
    {
        const Parsed p({ scenRecord(kScene, SceneFlags::BeginOnQuestStart | SceneFlags::StopQuestOnEnd,
            { phase(Bytes(), ctda(550, 1.f, kScene, 1), "first"), phase(ctda(59, 1.f, kQuest, 10)) }, { 1, 2 },
            { dialogueAction(1, 1, 0, kTopicA), packageAction(2, 2, 0, 1, { kPackage, kPackage2 }),
                timerAction(3, 1, 1, 2.5f) },
            kQuest) });
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mCount, 1u);
        const SceneRecord* s = p.mIndex.scene(kScene);
        ASSERT_NE(s, nullptr);
        EXPECT_EQ(s->mFlags, 3u);
        EXPECT_EQ(s->mQuest, kQuest);
        ASSERT_EQ(s->mPhases.size(), 2u);
        EXPECT_EQ(s->mPhases[0].mName, "first");
        EXPECT_TRUE(s->mPhases[0].mStartConditions.empty());
        ASSERT_EQ(s->mPhases[0].mCompletionConditions.size(), 1u);
        EXPECT_EQ(s->mPhases[0].mCompletionConditions[0].mFunction, 550u);
        ASSERT_EQ(s->mPhases[1].mStartConditions.size(), 1u);
        EXPECT_TRUE(s->mPhases[1].mCompletionConditions.empty());
        ASSERT_EQ(s->mActors.size(), 2u);
        EXPECT_EQ(s->mActors[1].mAlias, 2u);
        ASSERT_EQ(s->mActions.size(), 3u);
        EXPECT_EQ(s->mActions[0].mType, SceneActionType::Dialogue);
        EXPECT_EQ(s->mActions[0].mTopic, kTopicA);
        EXPECT_EQ(s->mActions[0].mAlias, 1u);
        EXPECT_EQ(s->mActions[1].mType, SceneActionType::Package);
        EXPECT_EQ(s->mActions[1].mPackages, (std::vector<FormId>{ kPackage, kPackage2 }));
        EXPECT_EQ(s->mActions[1].mEndPhase, 1u);
        EXPECT_EQ(s->mActions[2].mType, SceneActionType::Timer);
        EXPECT_TRUE(s->mActions[2].mHasSeconds);
        EXPECT_FLOAT_EQ(s->mActions[2].mSeconds, 2.5f);
        EXPECT_EQ(s->mActions[2].mStartPhase, 1u);
        EXPECT_EQ(s->actionByIndex(3)->mType, SceneActionType::Timer);
        EXPECT_EQ(s->actionByIndex(9), nullptr);
        EXPECT_NE(s->actor(2), nullptr);
        EXPECT_EQ(p.mRecords.mOddSubrecords, 0u);
    }

    TEST(SceneDecode, theScenesOwnConditionsAreTheOnesAfterVnam)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase() }, { 1 }, {}, kQuest, ctda(58, 20.f, kQuest)) });
        const SceneRecord* s = p.mIndex.scene(kScene);
        ASSERT_NE(s, nullptr);
        ASSERT_EQ(s->mConditions.size(), 1u);
        EXPECT_EQ(s->mConditions[0].mFunction, 58u);
        EXPECT_TRUE(s->mPhases[0].mStartConditions.empty());
        EXPECT_TRUE(s->mPhases[0].mCompletionConditions.empty());
    }

    TEST(SceneDecode, aDialogueActionWithNoTopicKeepsTopicZero)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase() }, { 1 }, { dialogueAction(1, 1, 0, 0) }, kQuest) });
        const SceneRecord* s = p.mIndex.scene(kScene);
        ASSERT_NE(s, nullptr);
        ASSERT_EQ(s->mActions.size(), 1u);
        EXPECT_EQ(s->mActions[0].mTopic, kNullFormId);
    }

    TEST(SceneDecode, scenesOfAQuestAreListedAndALaterRecordReplacesAnEarlierOne)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase() }, { 1 }, {}, kQuest),
            scenRecord(kScene2, 0, { phase(), phase() }, { 1 }, {}, kQuest) });
        EXPECT_EQ(p.mIndex.scenesOfQuest(kQuest), (std::vector<FormId>{ kScene, kScene2 }));
        EXPECT_EQ(p.mIndex.count(), 2u);
        EXPECT_TRUE(p.mIndex.scenesOfQuest(kSelf | 0x1).empty());
    }

    TEST(SceneDecode, aDamagedConditionMarksThePhaseAndAWrongSizedSubrecordIsOdd)
    {
        Bytes bad = sub4("CTDA", Bytes().u32(1));
        const Parsed p({ scenRecord(kScene, 0, { phase(bad) }, { 1 }, {}, kQuest) });
        const SceneRecord* s = p.mIndex.scene(kScene);
        ASSERT_NE(s, nullptr);
        EXPECT_FALSE(s->mPhases[0].mConditionsOk);
    }

    // ---- the fragment VMAD ----------------------------------------------------------------------------------------

    void wstr(Bytes& b, const std::string& s)
    {
        b.u16(static_cast<std::uint16_t>(s.size()));
        for (char c : s)
            b.u8(static_cast<std::uint8_t>(c));
    }

    Bytes sceneVmad(int flags, const std::vector<std::pair<int, int>>& phases, bool section = true, bool extra = false)
    {
        Bytes b;
        b.u16(5).u16(2).u16(0); // no scripts
        if (!section)
            return b;
        b.u8(2).u8(static_cast<std::uint8_t>(flags));
        wstr(b, "SF_Test_00000900");
        if ((flags & 1) != 0)
        {
            b.u8(1);
            wstr(b, "SF_Test_00000900");
            wstr(b, "Fragment_0");
        }
        if ((flags & 2) != 0)
        {
            b.u8(1);
            wstr(b, "SF_Test_00000900");
            wstr(b, "Fragment_1");
        }
        b.u16(static_cast<std::uint16_t>(phases.size()));
        for (const auto& [pflags, index] : phases)
        {
            b.u8(static_cast<std::uint8_t>(pflags)).u8(static_cast<std::uint8_t>(index)).u16(0).u8(0).u8(1);
            wstr(b, "SF_Test_00000900");
            wstr(b, "Fragment_" + std::to_string(index + 2));
        }
        if (extra)
            b.u8(9);
        return b;
    }

    std::vector<std::byte> bytesOf(const Bytes& b)
    {
        const std::string s = b.str();
        std::vector<std::byte> out(s.size());
        std::memcpy(out.data(), s.data(), s.size());
        return out;
    }

    TEST(SceneVmad, beginEndAndPhaseFragmentsAreRead)
    {
        const vmad::ParseResult r = vmad::parseScene(bytesOf(sceneVmad(3, { { 2, 1 }, { 1, 0 } })));
        ASSERT_TRUE(r.mOk) << r.mError;
        ASSERT_TRUE(r.mVmad.mHasSceneFragments);
        const vmad::SceneFragments& f = r.mVmad.mScene;
        EXPECT_EQ(f.mBeginFunction, "Fragment_0");
        EXPECT_EQ(f.mEndFunction, "Fragment_1");
        ASSERT_EQ(f.mPhases.size(), 2u);
        EXPECT_TRUE(f.mPhases[0].onCompletion());
        EXPECT_FALSE(f.mPhases[0].onStart());
        EXPECT_EQ(f.mPhases[0].mPhase, 1u);
        EXPECT_EQ(f.mPhases[0].mFunction, "Fragment_3");
        EXPECT_TRUE(f.mPhases[1].onStart());
    }

    TEST(SceneVmad, aVmadWithNoSectionIsScriptsOnly)
    {
        const vmad::ParseResult r = vmad::parseScene(bytesOf(sceneVmad(0, {}, false)));
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_FALSE(r.mVmad.mHasSceneFragments);
    }

    TEST(SceneVmad, trailingBytesAnUnknownFlagAndEveryTruncationAreRefused)
    {
        EXPECT_FALSE(vmad::parseScene(bytesOf(sceneVmad(3, { { 2, 1 } }, true, true))).mOk);
        EXPECT_FALSE(vmad::parseScene(bytesOf(sceneVmad(4, {}))).mOk);
        const std::vector<std::byte> whole = bytesOf(sceneVmad(3, { { 2, 1 }, { 1, 0 } }));
        for (std::size_t n = 7; n < whole.size(); ++n)
            EXPECT_FALSE(vmad::parseScene(std::span<const std::byte>(whole.data(), n)).mOk) << n;
    }

    // ---- the runner -----------------------------------------------------------------------------------------------

    class FakeEnv final : public SceneEnvironment
    {
    public:
        bool questRunning(FormId quest) const override { return mQuestRunning && quest == kQuest; }
        FormId aliasReference(FormId, std::uint32_t alias) const override
        {
            const auto it = mAliases.find(alias);
            return it == mAliases.end() ? kNullFormId : it->second;
        }
        bool evaluate(const std::vector<survey::Ctda>& list, FormId, FormId) override
        {
            ++mEvaluations;
            if (list.empty())
                return true;
            return mConditionResult;
        }
        bool resolveLine(FormId, FormId topic, FormId, SceneLine& out) override
        {
            const auto it = mLines.find(topic);
            if (it == mLines.end())
                return false;
            out = it->second;
            return true;
        }
        bool speak(FormId speaker, const SceneLine& line) override
        {
            if (!mLoaded)
                return false;
            mLog.push_back("say " + std::to_string(speaker & 0xFFF) + " " + line.mText);
            return true;
        }
        void hold(FormId actor, bool on) override
        {
            mLog.push_back(std::string(on ? "hold " : "release ") + std::to_string(actor & 0xFFF));
        }
        bool setPackage(FormId actor, FormId package) override
        {
            mLog.push_back("package " + std::to_string(actor & 0xFFF) + " " + std::to_string(package & 0xFFF));
            return true;
        }
        void clearPackage(FormId actor) override { mLog.push_back("clear " + std::to_string(actor & 0xFFF)); }
        bool packageFinished(FormId) override { return mPackageDone; }
        void runFragment(FormId, const std::string&, const std::string& function) override
        {
            mLog.push_back("fragment " + function);
        }
        void stopQuest(FormId) override { mLog.push_back("stopQuest"); }
        void log(const std::string&) override {}

        bool mQuestRunning = true;
        bool mConditionResult = true;
        bool mLoaded = true;
        bool mPackageDone = false;
        int mEvaluations = 0;
        std::map<std::uint32_t, FormId> mAliases{ { 1, kRefHulda }, { 2, kRefMikael } };
        std::map<FormId, SceneLine> mLines;
        std::vector<std::string> mLog;
    };

    SceneLine line(const std::string& text)
    {
        return SceneLine{ text, { "a/" + text + ".fuz" } };
    }

    // dialogue in phases 0 and 1, spoken by alias 1 then alias 2
    Parsed twoLines(std::uint32_t flags = 0, const Bytes& vmad = Bytes(), const Bytes& completion0 = Bytes())
    {
        return Parsed({ scenRecord(kScene, flags, { phase(Bytes(), completion0), phase() }, { 1, 2 },
            { dialogueAction(1, 1, 0, kTopicA), dialogueAction(2, 2, 1, kTopicB) }, kQuest, Bytes(), vmad) });
    }

    // 10 characters: 1.5 s minimum, plus the 0.4 s gap
    void tick(SceneRunner& r, float seconds, int steps = 1)
    {
        for (int i = 0; i < steps; ++i)
            r.update(seconds);
    }

    TEST(SceneRunnerTest, twoLinesAreSpokenInOrderAndTheSceneEnds)
    {
        const Parsed p = twoLines();
        FakeEnv env;
        env.mLines[kTopicA] = line("hello");
        env.mLines[kTopicB] = line("goodbye");
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        EXPECT_TRUE(runner.isPlaying(kScene));
        tick(runner, 0.1f);
        ASSERT_FALSE(env.mLog.empty());
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3073 hello"), 1);
        tick(runner, 0.1f, 5); // inside the line: the second is not said yet
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3074 goodbye"), 0);
        tick(runner, 0.5f, 6); // past 1.9 s
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3074 goodbye"), 1);
        EXPECT_EQ(runner.phaseOf(kScene), 1);
        tick(runner, 0.5f, 6);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(runner.stats().mLines, 2u);
        EXPECT_EQ(runner.stats().mCompleted, 1u);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "release 3073"), 1);
    }

    TEST(SceneRunnerTest, aStartIsRefusedForAnUnknownSceneANonRunningQuestAndAnEmptyActorList)
    {
        const Parsed p = twoLines();
        FakeEnv env;
        SceneRunner runner(p.mIndex, env);
        EXPECT_FALSE(runner.start(kSelf | 0x999));
        env.mQuestRunning = false;
        EXPECT_FALSE(runner.start(kScene));
        env.mQuestRunning = true;
        env.mAliases.clear();
        EXPECT_FALSE(runner.start(kScene));
        EXPECT_EQ(runner.stats().mRefused, 3u);
        env.mAliases = { { 1, kRefHulda } };
        EXPECT_TRUE(runner.start(kScene));
        EXPECT_FALSE(runner.start(kScene)) << "already playing";
    }

    TEST(SceneRunnerTest, theScenesConditionsGateAStartUnlessForced)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase() }, { 1 }, {}, kQuest, ctda(58, 20.f, kQuest)) });
        FakeEnv env;
        env.mConditionResult = false;
        SceneRunner runner(p.mIndex, env);
        EXPECT_FALSE(runner.start(kScene));
        EXPECT_TRUE(runner.start(kScene, true));
    }

    TEST(SceneRunnerTest, aDialogueActionWithNoTopicIsCompleteAtOnce)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase() }, { 1 }, { dialogueAction(1, 1, 0, 0) }, kQuest) });
        FakeEnv env;
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.1f, 3);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(runner.stats().mLinesSilent, 1u);
        EXPECT_EQ(runner.stats().mLines, 0u);
    }

    TEST(SceneRunnerTest, aPhaseWaitsForItsCompletionConditionsAndActionsOnlyIgnoresThem)
    {
        const Parsed p = twoLines(0, Bytes(), ctda(629, 1.f, kQuest));
        {
            FakeEnv env;
            env.mLines[kTopicA] = line("hello");
            env.mLines[kTopicB] = line("goodbye");
            env.mConditionResult = false;
            SceneRunner runner(p.mIndex, env);
            ASSERT_TRUE(runner.start(kScene));
            tick(runner, 0.5f, 20);
            EXPECT_EQ(runner.phaseOf(kScene), 0) << "the condition is false";
            env.mConditionResult = true;
            tick(runner, 0.5f, 2);
            EXPECT_EQ(runner.phaseOf(kScene), 1);
        }
        {
            FakeEnv env;
            env.mLines[kTopicA] = line("hello");
            env.mLines[kTopicB] = line("goodbye");
            env.mConditionResult = false;
            SceneRunner::Options options;
            options.mPhaseEnd = PhaseEndRule::ActionsOnly;
            SceneRunner runner(p.mIndex, env, options);
            ASSERT_TRUE(runner.start(kScene));
            tick(runner, 0.5f, 6);
            EXPECT_EQ(runner.phaseOf(kScene), 1);
        }
    }

    TEST(SceneRunnerTest, aPhaseWithStartConditionsWaitsToBegin)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase(), phase(ctda(59, 1.f, kQuest, 10)) }, { 1 },
            { dialogueAction(1, 1, 0, kTopicA), dialogueAction(1, 2, 1, kTopicB) }, kQuest) });
        FakeEnv env;
        env.mLines[kTopicA] = line("one");
        env.mLines[kTopicB] = line("two");
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.1f); // phase 0 begins with no conditions
        env.mConditionResult = false;
        tick(runner, 0.5f, 10);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3073 two"), 0);
        env.mConditionResult = true;
        tick(runner, 0.5f, 3);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3073 two"), 1);
    }

    TEST(SceneRunnerTest, aTimerActionHoldsItsPhaseForItsSeconds)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase() }, { 1 }, { timerAction(1, 0, 0, 3.f) }, kQuest) });
        FakeEnv env;
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene, true));
        tick(runner, 1.f, 2);
        EXPECT_TRUE(runner.isPlaying(kScene));
        tick(runner, 1.f, 2);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(runner.stats().mTimers, 1u);
    }

    TEST(SceneRunnerTest, aPackageActionIsGivenAndEndsWhenTheActorFinishedIt)
    {
        const Parsed p(
            { scenRecord(kScene, 0, { phase() }, { 1 }, { packageAction(1, 1, 0, 0, { kPackage }) }, kQuest) });
        FakeEnv env;
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.5f, 4);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "package 3073 2817"), 1);
        EXPECT_TRUE(runner.isPlaying(kScene));
        env.mPackageDone = true;
        tick(runner, 0.5f, 2);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "clear 3073"), 1);
    }

    TEST(SceneRunnerTest, aPackageThatNeverFinishesEndsAfterItsTimeout)
    {
        const Parsed p(
            { scenRecord(kScene, 0, { phase() }, { 1 }, { packageAction(1, 1, 0, 0, { kPackage }) }, kQuest) });
        FakeEnv env;
        SceneRunner::Options options;
        options.mPackageSeconds = 10.f;
        SceneRunner runner(p.mIndex, env, options);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 1.f, 8);
        EXPECT_TRUE(runner.isPlaying(kScene));
        tick(runner, 1.f, 5);
        EXPECT_FALSE(runner.isPlaying(kScene));
    }

    TEST(SceneRunnerTest, aSpeakerThatIsNotLoadedIsWaitedForThenSkipped)
    {
        const Parsed p = twoLines();
        FakeEnv env;
        env.mLines[kTopicA] = line("hello");
        env.mLines[kTopicB] = line("goodbye");
        env.mLoaded = false;
        SceneRunner::Options options;
        options.mParkSeconds = 5.f;
        SceneRunner runner(p.mIndex, env, options);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 1.f, 4);
        EXPECT_EQ(runner.phaseOf(kScene), 0);
        EXPECT_GE(runner.stats().mParked, 1u);
        env.mLoaded = true;
        tick(runner, 1.f, 2);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3073 hello"), 1) << "spoken once it loaded";
    }

    TEST(SceneRunnerTest, aSpeakerThatNeverLoadsExpiresAfterThePark)
    {
        const Parsed p = twoLines();
        FakeEnv env;
        env.mLines[kTopicA] = line("hello");
        env.mLines[kTopicB] = line("goodbye");
        env.mLoaded = false;
        SceneRunner::Options options;
        options.mParkSeconds = 3.f;
        SceneRunner runner(p.mIndex, env, options);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 1.f, 10);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(runner.stats().mParkExpired, 2u);
        EXPECT_EQ(runner.stats().mLines, 0u);
    }

    TEST(SceneRunnerTest, fragmentsRunAtTheBeginThePhasesAndTheEndAndTheQuestStopsWhenAsked)
    {
        const Bytes vmad = sceneVmad(3, { { 1, 0 }, { 2, 0 }, { 2, 1 } });
        const Parsed p = twoLines(SceneFlags::StopQuestOnEnd, vmad);
        FakeEnv env;
        env.mLines[kTopicA] = line("hello");
        env.mLines[kTopicB] = line("goodbye");
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.5f, 30);
        std::vector<std::string> fragments;
        for (const std::string& s : env.mLog)
            if (s.rfind("fragment", 0) == 0 || s == "stopQuest")
                fragments.push_back(s);
        const std::vector<std::string> expected = { "fragment Fragment_0", "fragment Fragment_2", "fragment Fragment_2",
            "fragment Fragment_3", "fragment Fragment_1", "stopQuest" };
        EXPECT_EQ(fragments, expected);
    }

    TEST(SceneRunnerTest, aQuestStartBeginsItsScenesThatSaySoAndAQuestStopEndsThem)
    {
        const Parsed p({ scenRecord(kScene, SceneFlags::BeginOnQuestStart, { phase() }, { 1 },
                             { timerAction(1, 0, 0, 5.f) }, kQuest),
            scenRecord(kScene2, 0, { phase() }, { 1 }, { timerAction(1, 0, 0, 5.f) }, kQuest) });
        FakeEnv env;
        SceneRunner runner(p.mIndex, env);
        EXPECT_EQ(runner.questStarted(kQuest), 1u);
        EXPECT_TRUE(runner.isPlaying(kScene));
        EXPECT_FALSE(runner.isPlaying(kScene2));
        runner.questStopped(kQuest);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(runner.stats().mStopped, 1u);
    }

    TEST(SceneRunnerTest, stopEndsASceneWithoutItsEndFragmentAndReleasesItsActors)
    {
        const Parsed p = twoLines(0, sceneVmad(3, {}));
        FakeEnv env;
        env.mLines[kTopicA] = line("hello");
        env.mLines[kTopicB] = line("goodbye");
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.1f);
        EXPECT_TRUE(runner.stop(kScene));
        EXPECT_FALSE(runner.stop(kScene));
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "fragment Fragment_1"), 0);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "release 3073"), 1);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "release 3074"), 1);
    }

    TEST(SceneRunnerTest, actionCompleteAnswersForActionsThatEnded)
    {
        const Parsed p = twoLines();
        FakeEnv env;
        env.mLines[kTopicA] = line("hello");
        env.mLines[kTopicB] = line("goodbye");
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.1f);
        EXPECT_FALSE(runner.actionComplete(kScene, 1));
        tick(runner, 0.5f, 6);
        EXPECT_TRUE(runner.actionComplete(kScene, 1));
        EXPECT_FALSE(runner.actionComplete(kScene, 2));
        tick(runner, 0.5f, 10);
        EXPECT_TRUE(runner.actionComplete(kScene, 2)) << "still answered after the scene ended";
        EXPECT_FALSE(runner.actionComplete(kScene, 77));
    }

    TEST(SceneRunnerTest, twoRunsGiveTheSameLog)
    {
        const Parsed p = twoLines(SceneFlags::StopQuestOnEnd, sceneVmad(3, { { 2, 0 } }));
        std::vector<std::string> logs[2];
        for (int run = 0; run < 2; ++run)
        {
            FakeEnv env;
            env.mLines[kTopicA] = line("hello");
            env.mLines[kTopicB] = line("goodbye");
            SceneRunner runner(p.mIndex, env);
            ASSERT_TRUE(runner.start(kScene));
            tick(runner, 0.1f, 80);
            logs[run] = env.mLog;
        }
        EXPECT_FALSE(logs[0].empty());
        EXPECT_EQ(logs[0], logs[1]);
    }

    // Task 0287: how long a scene waits for a speaker that is not loaded is a setting; a bad number is cut to a range.
    TEST(SceneRunnerPark, theDefaultIsTheOldTwoMinutes)
    {
        EXPECT_FLOAT_EQ(SceneRunner::Options().mParkSeconds, 120.f);
        EXPECT_FLOAT_EQ(SceneRunner::clampParkSeconds(120.f), 120.f);
    }

    // Task 0316: with `quest alias packages` the park time defaults to 5 s; a set value wins; else the old default.
    TEST(SceneRunnerPark, questAliasPackagesDefaultToFiveSecondsAndASetValueWins)
    {
        EXPECT_FLOAT_EQ(SceneRunner::parkSecondsFor(0.f, true), 5.f);
        EXPECT_FLOAT_EQ(SceneRunner::parkSecondsFor(0.f, false), 0.f);
        EXPECT_FLOAT_EQ(SceneRunner::parkSecondsFor(30.f, true), 30.f);
        EXPECT_FLOAT_EQ(SceneRunner::parkSecondsFor(30.f, false), 30.f);
        EXPECT_FLOAT_EQ(SceneRunner::parkSecondsFor(-1.f, true), 5.f);
    }

    TEST(SceneRunnerPark, aBadNumberIsCutToTheRange)
    {
        EXPECT_FLOAT_EQ(SceneRunner::clampParkSeconds(0.f), SceneRunner::kMinParkSeconds);
        EXPECT_FLOAT_EQ(SceneRunner::clampParkSeconds(-4.f), SceneRunner::kMinParkSeconds);
        EXPECT_FLOAT_EQ(SceneRunner::clampParkSeconds(1e9f), SceneRunner::kMaxParkSeconds);
        EXPECT_FLOAT_EQ(SceneRunner::clampParkSeconds(std::numeric_limits<float>::quiet_NaN()), 120.f);
        EXPECT_FLOAT_EQ(SceneRunner::clampParkSeconds(5.f), 5.f);
    }

    TEST(SceneRunnerPark, aShortParkLetsAnAbsentSpeakersLineGoQuickly)
    {
        const Parsed p = twoLines();
        FakeEnv env;
        env.mLines[kTopicA] = line("hello");
        env.mLines[kTopicB] = line("goodbye");
        env.mLoaded = false;
        SceneRunner runner(p.mIndex, env, SceneRunner::Options());
        runner.setParkSeconds(4.f);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 1.f, 12);
        EXPECT_FALSE(runner.isPlaying(kScene)) << "both lines expired within 12 s";
        EXPECT_EQ(runner.stats().mParkExpired, 2u);
    }

    // ---- Task 0308: a phase whose start conditions fail is skipped (the Whiterun scene's phase 7 needs stage 82, which
    // only one dialogue path sets; the scene must still reach its last phase)
    TEST(SceneRunnerSkip, aPhaseWhoseStartConditionsFailIsSkippedWhenAsked)
    {
        const FormId kTopicC = kSelf | 0xA03;
        const Parsed p({ scenRecord(kScene, 0, { phase(), phase(ctda(59, 1.f, kQuest, 10)), phase() }, { 1 },
            { dialogueAction(1, 1, 0, kTopicA), dialogueAction(1, 2, 1, kTopicB), dialogueAction(1, 3, 2, kTopicC) },
            kQuest) });
        FakeEnv env;
        env.mLines[kTopicA] = line("one");
        env.mLines[kTopicB] = line("two");
        env.mLines[kTopicC] = line("three");
        SceneRunner runner(p.mIndex, env);
        runner.setSkipFailedPhases(true);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.1f);
        env.mConditionResult = false;
        tick(runner, 0.5f, 20);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3073 two"), 0);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3073 three"), 1);
    }

    TEST(SceneRunnerSkip, withoutTheOptionAFailingStartConditionStillWaits)
    {
        const FormId kTopicC = kSelf | 0xA03;
        const Parsed p({ scenRecord(kScene, 0, { phase(), phase(ctda(59, 1.f, kQuest, 10)), phase() }, { 1 },
            { dialogueAction(1, 1, 0, kTopicA), dialogueAction(1, 2, 1, kTopicB), dialogueAction(1, 3, 2, kTopicC) },
            kQuest) });
        FakeEnv env;
        env.mLines[kTopicA] = line("one");
        env.mLines[kTopicB] = line("two");
        env.mLines[kTopicC] = line("three");
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.1f);
        env.mConditionResult = false;
        tick(runner, 0.5f, 20);
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "say 3073 three"), 0);
    }
}
