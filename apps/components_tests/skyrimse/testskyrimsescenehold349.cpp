// Task 0349 (MQ106 "A Blade in the Dark"): the intro scene's phase 6 waits for the player with Delphine in the bedroom trigger, so her
// finished Travel package must hold her there until the scene ends (the engine leaves her where she stands, it does not send her to sandbox).
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

    void tick(SceneRunner& r, float seconds, int steps = 1)
    {
        for (int i = 0; i < steps; ++i)
            r.update(seconds);
    }

    TEST(SceneRunnerHold349, withTheOptionAFinishedPackageIsNotClearedUntilTheSceneEnds)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase(), phase() }, { 1 },
            { packageAction(1, 1, 0, 0, { kPackage }), timerAction(2, 1, 1, 3.f) }, kQuest) });
        FakeEnv env;
        SceneRunner runner(p.mIndex, env);
        runner.setHoldFinishedPackages(true);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.5f, 2);
        env.mPackageDone = true;
        tick(runner, 0.5f, 4); // the package finishes, phase 1's timer is running
        ASSERT_TRUE(runner.isPlaying(kScene));
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "clear " + std::to_string(kRefHulda & 0xFFF)), 0);
        tick(runner, 0.5f, 12);
        EXPECT_FALSE(runner.isPlaying(kScene));
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "clear " + std::to_string(kRefHulda & 0xFFF)), 1);
    }

    TEST(SceneRunnerHold349, withoutTheOptionAFinishedPackageIsClearedAtOnce)
    {
        const Parsed p({ scenRecord(kScene, 0, { phase(), phase() }, { 1 },
            { packageAction(1, 1, 0, 0, { kPackage }), timerAction(2, 1, 1, 3.f) }, kQuest) });
        FakeEnv env;
        SceneRunner runner(p.mIndex, env);
        ASSERT_TRUE(runner.start(kScene));
        tick(runner, 0.5f, 2);
        env.mPackageDone = true;
        tick(runner, 0.5f, 4);
        ASSERT_TRUE(runner.isPlaying(kScene));
        EXPECT_EQ(std::count(env.mLog.begin(), env.mLog.end(), "clear " + std::to_string(kRefHulda & 0xFFF)), 1);
    }
}
