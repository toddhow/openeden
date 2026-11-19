// Task 0329 (Solstheim, third slice): a scene phase that lists completion conditions ends when they pass, whatever
// its other actions still do (`PhaseEndRule::ConditionsWhenPresent`; the Raven Rock arrival scene's phase 5).
// Generated records; no game data. Written first and run red against a stub rule.
#include <game_skyrimse/dragonborn.hpp>
#include <game_skyrimse/scene.hpp>
#include <game_skyrimse/scene_runner.hpp>

#include <gtest/gtest.h>

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
    constexpr FormId kRefA = kSelf | 0xC01;

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

    Bytes ctda()
    {
        Bytes b;
        b.u8(0x00).u8(0).u8(0).u8(0).f32(1.f);
        b.u16(550).u16(0).u32(kScene).u32(1).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    Bytes phase(const Bytes& completion = Bytes())
    {
        Bytes b;
        b.append(empty("HNAM"));
        b.append(sub4("NAM0", Bytes().zstring("")));
        b.append(empty("NEXT"));
        b.append(completion);
        b.append(empty("NEXT"));
        b.append(sub4("WNAM", Bytes().u32(200)));
        b.append(empty("HNAM"));
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

    Bytes dialogueAction(std::uint32_t alias, std::uint32_t index, std::uint32_t phaseIndex, FormId topic)
    {
        Bytes b = actionHead(0, alias, index, phaseIndex, phaseIndex);
        b.append(sub4("DATA", Bytes().u32(topic)));
        b.append(sub4("HTID", Bytes().u32(0xFFFFFFFFu)));
        b.append(sub4("DMAX", Bytes().f32(10.f)));
        b.append(sub4("DMIN", Bytes().f32(1.f)));
        b.append(sub4("DEMO", Bytes().u32(0)));
        b.append(sub4("DEVA", Bytes().u32(0)));
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

    Bytes scenRecord(const std::vector<Bytes>& phases, const std::vector<Bytes>& actions)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("Scene")));
        b.append(sub4("FNAM", Bytes().u32(0)));
        for (const Bytes& p : phases)
            b.append(p);
        b.append(sub4("ALID", Bytes().u32(1)));
        b.append(sub4("LNAM", Bytes().u32(0)));
        b.append(sub4("DNAM", Bytes().u32(26)));
        for (const Bytes& a : actions)
            b.append(a);
        b.append(sub4("PNAM", Bytes().u32(kQuest)));
        b.append(sub4("INAM", Bytes().u32(static_cast<std::uint32_t>(actions.size()))));
        b.append(sub4("VNAM", Bytes().u32(3).u32(3).u32(3).u32(3)));
        return record4("SCEN", 0u, kScene, b);
    }

    struct Parsed
    {
        explicit Parsed(const Bytes& record)
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("SCEN"), 0, record));
            Content::Error error;
            SceneRecords records;
            mOk = parseSceneRecordsBytes(bytes.str(), "test.esp", order(), SceneLimits(), records, error);
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            if (plugin != nullptr)
                mIndex.add(std::move(records), FormIdResolver(plugin->header(), "test.esp", order()));
        }
        bool mOk = false;
        SceneIndex mIndex;
    };

    class Env final : public SceneEnvironment
    {
    public:
        bool questRunning(FormId quest) const override { return quest == kQuest; }
        FormId aliasReference(FormId, std::uint32_t) const override { return kRefA; }
        bool evaluate(const std::vector<survey::Ctda>& list, FormId, FormId) override
        {
            return list.empty() || mConditions;
        }
        bool resolveLine(FormId, FormId, FormId, SceneLine& out) override
        {
            out = SceneLine{ "second", { "a/second.fuz" } };
            return true;
        }
        bool speak(FormId, const SceneLine& line) override
        {
            mSaid.push_back(line.mText);
            return true;
        }
        void hold(FormId, bool) override {}
        bool setPackage(FormId, FormId) override { return true; }
        void clearPackage(FormId) override {}
        bool packageFinished(FormId) override { return false; }
        void runFragment(FormId, const std::string&, const std::string&) override {}
        void stopQuest(FormId) override {}
        void log(const std::string&) override {}

        bool mConditions = true;
        std::vector<std::string> mSaid;
    };

    // phase 0 holds a long timer (index 1) and lists a completion condition (the fake answers it by a flag);
    // phase 1 has a line
    Parsed sceneWithConditions()
    {
        return Parsed(
            scenRecord({ phase(ctda()), phase() }, { timerAction(1, 0, 0, 1000.f), dialogueAction(1, 2, 1, kTopicA) }));
    }

    int said(const Parsed& p, PhaseEndRule rule, bool conditions)
    {
        Env env;
        env.mConditions = conditions;
        SceneRunner::Options options;
        options.mPhaseEnd = rule;
        SceneRunner runner(p.mIndex, env, options);
        EXPECT_TRUE(runner.start(kScene));
        for (int i = 0; i < 30; ++i)
            runner.update(0.5f);
        return static_cast<int>(env.mSaid.size());
    }

    TEST(SkyrimSolstheimScene, aPhaseWithCompletionConditionsEndsWhenTheyPassWhateverItsActionsDo)
    {
        const Parsed p = sceneWithConditions();
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(said(p, PhaseEndRule::ConditionsWhenPresent, true), 1);
    }

    TEST(SkyrimSolstheimScene, itStaysWhileTheConditionsFail)
    {
        const Parsed p = sceneWithConditions();
        EXPECT_EQ(said(p, PhaseEndRule::ConditionsWhenPresent, false), 0);
    }

    TEST(SkyrimSolstheimScene, theOldRuleStillWaitsForTheTimer)
    {
        const Parsed p = sceneWithConditions();
        EXPECT_EQ(said(p, PhaseEndRule::ActionsAndConditions, true), 0);
    }

    TEST(SkyrimSolstheimScene, aPhaseWithNoConditionsEndsWhenItsActionsDo)
    {
        const Parsed p(
            scenRecord({ phase(), phase() }, { timerAction(1, 0, 0, 3.f), dialogueAction(1, 2, 1, kTopicA) }));
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(said(p, PhaseEndRule::ConditionsWhenPresent, true), 1);
    }
}

// ---- the location of an actor a script moved outdoors ----------------------------------------------------------

namespace
{
    using namespace game_skyrimse::dragonborn;

    TEST(SkyrimSolstheimMoved, anActorMovedFromAnInteriorToThePlayersExteriorStandsOutdoors)
    {
        EXPECT_TRUE(movedActorStandsOutdoors(true, true, true));
    }

    TEST(SkyrimSolstheimMoved, anyMissingFactLeavesTheRecordedCell)
    {
        EXPECT_FALSE(movedActorStandsOutdoors(false, true, true)); // an exterior placement keeps its own cell
        EXPECT_FALSE(movedActorStandsOutdoors(true, false, true)); // nothing known about where he stands now
        EXPECT_FALSE(movedActorStandsOutdoors(true, true, false)); // the player is indoors: his interior is real
    }

    TEST(SkyrimSolstheimMoved, theGridSquareIsTheFloorOfTheCoordinateOverTheSquareSize)
    {
        EXPECT_EQ(gridSquareOf(23458.5), 5); // the Earth Stone marker x
        EXPECT_EQ(gridSquareOf(31375.2), 7);
        EXPECT_EQ(gridSquareOf(0.0), 0);
        EXPECT_EQ(gridSquareOf(4095.9), 0);
        EXPECT_EQ(gridSquareOf(4096.0), 1);
        EXPECT_EQ(gridSquareOf(-0.1), -1);
        EXPECT_EQ(gridSquareOf(-4096.0), -1);
        EXPECT_EQ(gridSquareOf(-4096.1), -2);
    }
}

namespace
{
    TEST(SkyrimSolstheimAid, oneNumberIsOneStageAsItAlwaysWas)
    {
        const TestStages one = parseTestStages("10");
        ASSERT_EQ(one.mStages.size(), 1u);
        EXPECT_EQ(one.mStages[0], 10);
        EXPECT_DOUBLE_EQ(one.mDelay, 0.0);
        const TestStages timed = parseTestStages("10,45");
        ASSERT_EQ(timed.mStages.size(), 1u);
        EXPECT_EQ(timed.mStages[0], 10);
        EXPECT_DOUBLE_EQ(timed.mDelay, 45.0);
    }

    TEST(SkyrimSolstheimAid, aListSetsTheStagesInOrder)
    {
        const TestStages list = parseTestStages("5+7+10,40");
        ASSERT_EQ(list.mStages.size(), 3u);
        EXPECT_EQ(list.mStages[0], 5);
        EXPECT_EQ(list.mStages[1], 7);
        EXPECT_EQ(list.mStages[2], 10);
        EXPECT_DOUBLE_EQ(list.mDelay, 40.0);
    }

    TEST(SkyrimSolstheimAid, noNumberOrZeroOrANegativeMeansNoStage)
    {
        EXPECT_TRUE(parseTestStages("").mStages.empty());
        EXPECT_TRUE(parseTestStages("abc").mStages.empty());
        EXPECT_TRUE(parseTestStages("0,5").mStages.empty());
        const TestStages mixed = parseTestStages("5+x+-3+10");
        ASSERT_EQ(mixed.mStages.size(), 2u);
        EXPECT_EQ(mixed.mStages[0], 5);
        EXPECT_EQ(mixed.mStages[1], 10);
    }
}
