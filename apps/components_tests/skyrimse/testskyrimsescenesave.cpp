// Task 0211: a scene that is playing at a save resumes at its phase after a load. The bytes of the save (a round trip, every
// truncation, hostile counts) and the runner's saveState and restoreState behind a fake environment. Generated records; no
// game data. Written first and run red against stubs.
#include <game_skyrimse/scene.hpp>
#include <game_skyrimse/scene_runner.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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
    constexpr FormId kQuest = kSelf | 0x800;
    constexpr FormId kTopicA = kSelf | 0xA01;
    constexpr FormId kTopicB = kSelf | 0xA02;
    constexpr FormId kTopicC = kSelf | 0xA03;
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

    Bytes empty(const char (&type)[5]) { return sub4(type, Bytes()); }

    Bytes phase()
    {
        Bytes b;
        b.append(empty("HNAM"));
        b.append(sub4("NAM0", Bytes().zstring("")));
        b.append(empty("NEXT"));
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

    Bytes actionHead(std::uint16_t type, std::uint32_t alias, std::uint32_t index, std::uint32_t start, std::uint32_t end)
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

    Bytes dialogueAction(std::uint32_t alias, std::uint32_t index, std::uint32_t phaseNumber, FormId topic)
    {
        Bytes b = actionHead(0, alias, index, phaseNumber, phaseNumber);
        b.append(sub4("DATA", Bytes().u32(topic)));
        b.append(sub4("HTID", Bytes().u32(0xFFFFFFFFu)));
        b.append(sub4("DMAX", Bytes().f32(10.f)));
        b.append(sub4("DMIN", Bytes().f32(1.f)));
        b.append(sub4("DEMO", Bytes().u32(0)));
        b.append(sub4("DEVA", Bytes().u32(0)));
        b.append(empty("ANAM"));
        return b;
    }

    Bytes scenRecord(FormId id, const std::vector<Bytes>& phases, const std::vector<std::uint32_t>& actors,
        const std::vector<Bytes>& actions, FormId quest)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("Scene" + std::to_string(id & 0xFFF))));
        b.append(sub4("FNAM", Bytes().u32(0)));
        for (const Bytes& p : phases)
            b.append(p);
        for (std::uint32_t a : actors)
            b.append(actor(a));
        for (const Bytes& a : actions)
            b.append(a);
        b.append(sub4("PNAM", Bytes().u32(quest)));
        b.append(sub4("INAM", Bytes().u32(static_cast<std::uint32_t>(actions.size()))));
        b.append(sub4("VNAM", Bytes().u32(3).u32(3).u32(3).u32(3)));
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
            SceneRecords records2;
            mOk = parseSceneRecordsBytes(bytes.str(), "test.esp", order(), SceneLimits(), records2, error);
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            if (plugin != nullptr)
                mIndex.add(std::move(records2), FormIdResolver(plugin->header(), "test.esp", order()));
        }
        bool mOk = false;
        SceneIndex mIndex;
    };

    class FakeEnv final : public SceneEnvironment
    {
    public:
        bool questRunning(FormId quest) const override { return mQuestRunning && quest == kQuest; }
        FormId aliasReference(FormId, std::uint32_t alias) const override
        {
            const auto it = mAliases.find(alias);
            return it == mAliases.end() ? kNullFormId : it->second;
        }
        bool evaluate(const std::vector<survey::Ctda>&, FormId, FormId) override
        {
            ++mEvaluations;
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
            mLog.push_back("say " + std::to_string(speaker & 0xFFF) + " " + line.mText);
            return true;
        }
        void hold(FormId actor, bool on) override
        {
            mLog.push_back(std::string(on ? "hold " : "release ") + std::to_string(actor & 0xFFF));
        }
        bool setPackage(FormId, FormId) override { return true; }
        void clearPackage(FormId) override {}
        bool packageFinished(FormId) override { return true; }
        void runFragment(FormId, const std::string&, const std::string& function) override
        {
            mLog.push_back("fragment " + function);
        }
        void stopQuest(FormId) override { mLog.push_back("stopQuest"); }
        void log(const std::string&) override {}

        bool mQuestRunning = true;
        bool mConditionResult = true;
        int mEvaluations = 0;
        std::map<std::uint32_t, FormId> mAliases{ { 1, kRefHulda }, { 2, kRefMikael } };
        std::map<FormId, SceneLine> mLines;
        std::vector<std::string> mLog;
    };

    SceneLine line(const std::string& text) { return SceneLine{ text, { "a/" + text + ".fuz" } }; }

    // three phases, one line in each, the speakers alternate
    Parsed threeLines()
    {
        return Parsed({ scenRecord(kScene, { phase(), phase(), phase() }, { 1, 2 },
            { dialogueAction(1, 1, 0, kTopicA), dialogueAction(2, 2, 1, kTopicB), dialogueAction(1, 3, 2, kTopicC) },
            kQuest) });
    }

    void fillLines(FakeEnv& env)
    {
        env.mLines[kTopicA] = line("one");
        env.mLines[kTopicB] = line("two");
        env.mLines[kTopicC] = line("three");
    }

    std::size_t count(const std::vector<std::string>& log, const std::string& what)
    {
        return static_cast<std::size_t>(std::count(log.begin(), log.end(), what));
    }

    // ---- the bytes --------------------------------------------------------------------------------------------------

    std::vector<SceneSaveEntry> sample()
    {
        return { SceneSaveEntry{ kScene, 2, true, { 2, 1, 0 } }, SceneSaveEntry{ kScene + 1, 0, false, {} } };
    }

    TEST(SceneSaveBytes, aStateRoundTripsAndTheEncodingIsStable)
    {
        const std::vector<std::uint8_t> bytes = encodeSceneState(sample());
        ASSERT_FALSE(bytes.empty());
        std::vector<SceneSaveEntry> back;
        std::string error;
        ASSERT_TRUE(decodeSceneState(bytes, back, error)) << error;
        ASSERT_EQ(back.size(), 2u);
        EXPECT_EQ(back[0].mScene, kScene);
        EXPECT_EQ(back[0].mPhase, 2);
        EXPECT_TRUE(back[0].mEntered);
        EXPECT_FALSE(back[1].mEntered);
        EXPECT_EQ(back[0].mActions, (std::vector<std::uint8_t>{ 2, 1, 0 }));
        EXPECT_TRUE(back[1].mActions.empty());
        EXPECT_EQ(encodeSceneState(back), bytes);
    }

    TEST(SceneSaveBytes, noScenesIsAZeroCountOfFourBytes)
    {
        const std::vector<std::uint8_t> bytes = encodeSceneState({});
        EXPECT_EQ(bytes, (std::vector<std::uint8_t>{ 0, 0, 0, 0 }));
        std::vector<SceneSaveEntry> back{ SceneSaveEntry{} };
        std::string error;
        ASSERT_TRUE(decodeSceneState(bytes, back, error)) << error;
        EXPECT_TRUE(back.empty());
    }

    TEST(SceneSaveBytes, theLayoutIsLittleEndianByHand)
    {
        const std::vector<std::uint8_t> bytes = encodeSceneState({ SceneSaveEntry{ 0x01020304u, 5, true, { 1, 2 } } });
        const std::vector<std::uint8_t> expected
            = { 1, 0, 0, 0, 0x04, 0x03, 0x02, 0x01, 5, 0, 0, 0, 1, 2, 0, 0, 0, 1, 2 };
        EXPECT_EQ(bytes, expected);
    }

    TEST(SceneSaveBytes, everyTruncationAndTrailingBytesAreRefused)
    {
        const std::vector<std::uint8_t> bytes = encodeSceneState(sample());
        for (std::size_t n = 0; n < bytes.size(); ++n)
        {
            std::vector<SceneSaveEntry> back;
            std::string error;
            EXPECT_FALSE(decodeSceneState(std::span<const std::uint8_t>(bytes.data(), n), back, error)) << n;
            EXPECT_FALSE(error.empty()) << n;
        }
        std::vector<std::uint8_t> longer = bytes;
        longer.push_back(0);
        std::vector<SceneSaveEntry> back;
        std::string error;
        EXPECT_FALSE(decodeSceneState(longer, back, error));
    }

    TEST(SceneSaveBytes, hostileCountsStagesAndPhasesAreRefused)
    {
        std::string error;
        std::vector<SceneSaveEntry> back;
        // 65 scenes
        std::vector<SceneSaveEntry> many;
        for (std::uint32_t i = 0; i < 65; ++i)
            many.push_back(SceneSaveEntry{ kScene + i, 0, false, {} });
        EXPECT_FALSE(decodeSceneState(encodeSceneState(many), back, error));
        // a stage byte of 3
        EXPECT_FALSE(decodeSceneState(encodeSceneState({ SceneSaveEntry{ kScene, 0, false, { 3 } } }), back, error));
        // a negative or huge phase
        EXPECT_FALSE(decodeSceneState(encodeSceneState({ SceneSaveEntry{ kScene, -1, false, {} } }), back, error));
        EXPECT_FALSE(decodeSceneState(encodeSceneState({ SceneSaveEntry{ kScene, 5000, false, {} } }), back, error));
        // 4097 actions
        EXPECT_FALSE(decodeSceneState(
            encodeSceneState({ SceneSaveEntry{ kScene, 0, false, std::vector<std::uint8_t>(4097, 0) } }), back, error));
        // the same scene twice
        EXPECT_FALSE(decodeSceneState(encodeSceneState({ SceneSaveEntry{ kScene, 0, false, {} }, SceneSaveEntry{ kScene, 1, false, {} } }),
            back, error));
        // a count that claims more than the bytes hold (4 GiB of actions)
        const std::vector<std::uint8_t> claim = { 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };
        EXPECT_FALSE(decodeSceneState(claim, back, error));
    }

    // ---- the runner -------------------------------------------------------------------------------------------------

    TEST(SceneSaveRunner, aPlayingSceneIsSavedAtItsPhaseWithTheStagesOfItsActions)
    {
        const Parsed p = threeLines();
        FakeEnv env;
        fillLines(env);
        SceneRunner runner(p.mIndex, env);
        EXPECT_TRUE(runner.saveState().empty());
        ASSERT_TRUE(runner.start(kScene));
        // 1.9 s per line: after 2.0 s phase 0 is done, phase 1 runs
        for (int i = 0; i < 20; ++i)
            runner.update(0.1f);
        ASSERT_EQ(runner.phaseOf(kScene), 1);
        const std::vector<SceneSaveEntry> saved = runner.saveState();
        ASSERT_EQ(saved.size(), 1u);
        EXPECT_EQ(saved[0].mScene, kScene);
        EXPECT_EQ(saved[0].mPhase, 1);
        EXPECT_TRUE(saved[0].mEntered);
        EXPECT_EQ(saved[0].mActions, (std::vector<std::uint8_t>{ 2, 1, 0 }));
    }

    TEST(SceneSaveRunner, aLoadResumesAtTheSavedPhaseWithoutRepeatingTheFinishedLines)
    {
        const Parsed p = threeLines();
        std::vector<SceneSaveEntry> saved;
        {
            FakeEnv env;
            fillLines(env);
            SceneRunner runner(p.mIndex, env);
            ASSERT_TRUE(runner.start(kScene));
            for (int i = 0; i < 20; ++i)
                runner.update(0.1f);
            saved = runner.saveState();
        }
        FakeEnv env;
        fillLines(env);
        SceneRunner runner(p.mIndex, env);
        const SceneRestoreResult result = runner.restoreState(saved);
        EXPECT_EQ(result.mResumed, 1u);
        EXPECT_EQ(result.mDropped, 0u);
        EXPECT_TRUE(runner.isPlaying(kScene));
        runner.update(0.1f);
        EXPECT_EQ(runner.phaseOf(kScene), 1);
        EXPECT_EQ(count(env.mLog, "say 3073 one"), 0u); // phase 0 is not said again
        EXPECT_EQ(count(env.mLog, "say 3074 two"), 1u); // phase 1 starts again
        for (int i = 0; i < 60; ++i)
            runner.update(0.1f);
        EXPECT_EQ(count(env.mLog, "say 3073 three"), 1u);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(runner.stats().mCompleted, 1u);
        EXPECT_TRUE(runner.actionComplete(kScene, 1)); // done in the first run: still answered after the load
    }

    TEST(SceneSaveRunner, theBeginFragmentAndConditionsAreNotRunAgainOnARestore)
    {
        const Parsed p = threeLines();
        FakeEnv env;
        fillLines(env);
        env.mConditionResult = false; // a forced start: conditions do not matter, and there are none on this scene
        SceneRunner runner(p.mIndex, env);
        const SceneRestoreResult result = runner.restoreState({ SceneSaveEntry{ kScene, 2, false, { 2, 2, 0 } } });
        EXPECT_EQ(result.mResumed, 1u);
        EXPECT_EQ(runner.phaseOf(kScene), 2);
        EXPECT_EQ(env.mEvaluations, 0);
        EXPECT_EQ(count(env.mLog, "hold 3073"), 1u);
    }

    TEST(SceneSaveRunner, anEnteredPhaseDoesNotStartItsActionsAgainAndAPhaseNotEnteredDoes)
    {
        const Parsed p = threeLines();
        {
            FakeEnv env;
            fillLines(env);
            SceneRunner runner(p.mIndex, env);
            ASSERT_EQ(runner.restoreState({ SceneSaveEntry{ kScene, 1, true, { 2, 0, 0 } } }).mResumed, 1u);
            for (int i = 0; i < 30; ++i)
                runner.update(0.1f);
            EXPECT_EQ(count(env.mLog, "say 3074 two"), 0u);
        }
        {
            FakeEnv env;
            fillLines(env);
            SceneRunner runner(p.mIndex, env);
            ASSERT_EQ(runner.restoreState({ SceneSaveEntry{ kScene, 1, false, { 2, 0, 0 } } }).mResumed, 1u);
            runner.update(0.1f);
            EXPECT_EQ(count(env.mLog, "say 3074 two"), 1u);
        }
    }

    TEST(SceneSaveRunner, stopAllEndsEverySceneWithoutAnEndFragmentAndReleasesTheActors)
    {
        const Parsed p = threeLines();
        FakeEnv env;
        fillLines(env);
        SceneRunner runner(p.mIndex, env);
        EXPECT_EQ(runner.stopAll(), 0u);
        ASSERT_TRUE(runner.start(kScene));
        runner.update(0.1f);
        EXPECT_EQ(runner.stopAll(), 1u);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(count(env.mLog, "release 3073"), 1u);
        EXPECT_EQ(count(env.mLog, "stopQuest"), 0u);
        EXPECT_EQ(runner.stats().mStopped, 1u);
        EXPECT_EQ(runner.stopAll(), 0u);
    }

    TEST(SceneSaveRunner, anUnknownSceneANotRunningQuestAndAWrongActionCountAreDroppedAndCounted)
    {
        const Parsed p = threeLines();
        {
            FakeEnv env;
            SceneRunner runner(p.mIndex, env);
            const SceneRestoreResult r = runner.restoreState({ SceneSaveEntry{ kScene + 7, 0, false, {} } });
            EXPECT_EQ(r.mDropped, 1u);
            EXPECT_EQ(r.mResumed, 0u);
        }
        {
            FakeEnv env;
            env.mQuestRunning = false;
            SceneRunner runner(p.mIndex, env);
            EXPECT_EQ(runner.restoreState({ SceneSaveEntry{ kScene, 0, false, { 0, 0, 0 } } }).mDropped, 1u);
            EXPECT_FALSE(runner.isPlaying(kScene));
        }
        {
            FakeEnv env;
            SceneRunner runner(p.mIndex, env);
            EXPECT_EQ(runner.restoreState({ SceneSaveEntry{ kScene, 0, false, { 0, 0 } } }).mDropped, 1u); // 2 for 3 actions
            EXPECT_EQ(runner.restoreState({ SceneSaveEntry{ kScene, 9, false, { 0, 0, 0 } } }).mDropped, 1u); // phase 9 of 3
            EXPECT_FALSE(runner.isPlaying(kScene));
        }
    }

    TEST(SceneSaveRunner, aSceneAlreadyPlayingIsNotStartedASecondTime)
    {
        const Parsed p = threeLines();
        FakeEnv env;
        fillLines(env);
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        EXPECT_EQ(runner.restoreState({ SceneSaveEntry{ kScene, 1, false, { 2, 0, 0 } } }).mDropped, 1u);
        EXPECT_EQ(runner.phaseOf(kScene), 0);
    }

    TEST(SceneSaveRunner, theSavedStateOfAResumedSceneEqualsTheOneItWasResumedFrom)
    {
        const Parsed p = threeLines();
        FakeEnv env;
        fillLines(env);
        SceneRunner runner(p.mIndex, env);
        const std::vector<SceneSaveEntry> in = { SceneSaveEntry{ kScene, 1, true, { 2, 1, 0 } } };
        ASSERT_EQ(runner.restoreState(in).mResumed, 1u);
        runner.update(0.0f);
        const std::vector<SceneSaveEntry> out = runner.saveState();
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].mPhase, 1);
        EXPECT_EQ(out[0].mActions, (std::vector<std::uint8_t>{ 2, 1, 0 }));
    }

    TEST(SceneSaveRunner, twoLoadsOfTheSameSaveGiveTheSameLog)
    {
        const Parsed p = threeLines();
        std::vector<std::string> logs[2];
        for (int run = 0; run < 2; ++run)
        {
            FakeEnv env;
            fillLines(env);
            SceneRunner runner(p.mIndex, env);
            ASSERT_EQ(runner.restoreState({ SceneSaveEntry{ kScene, 1, true, { 2, 1, 0 } } }).mResumed, 1u);
            for (int i = 0; i < 80; ++i)
                runner.update(0.1f);
            logs[run] = env.mLog;
        }
        EXPECT_EQ(logs[0], logs[1]);
        EXPECT_FALSE(logs[0].empty());
    }
}
