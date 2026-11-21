// Task 0333 (Skyrim side quest "Blood on the Ice", MS11): the pure parts. A scene with no actions begins although none
// of its actors is loaded (only with the option), and the global GameHour takes the core's hour of day. Generated
// records and a fake environment; no game data. Written first and run red against stubs
// (docs/tasks/0333-skyrim-blood-on-the-ice.md).
#include <game_skyrimse/bloodice.hpp>
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

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kSceneEmpty = 0x01000900;
    constexpr FormId kSceneTimer = 0x01000901;
    constexpr FormId kActor = 0x01000C01;

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

    // one phase, one actor entry (alias 1), and optionally one timer action
    Bytes sceneRecord(FormId id, FormId quest, bool withTimer)
    {
        Bytes phase;
        phase.append(empty("HNAM"));
        phase.append(sub4("NAM0", Bytes().zstring("")));
        phase.append(empty("NEXT"));
        phase.append(empty("NEXT"));
        phase.append(sub4("WNAM", Bytes().u32(200)));
        phase.append(empty("HNAM"));
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("Scene")));
        b.append(sub4("FNAM", Bytes().u32(SceneFlags::BeginOnQuestStart)));
        b.append(phase);
        b.append(sub4("ALID", Bytes().u32(1)));
        b.append(sub4("LNAM", Bytes().u32(0)));
        b.append(sub4("DNAM", Bytes().u32(26)));
        if (withTimer)
        {
            Bytes timer;
            timer.append(sub4("ANAM", Bytes().u16(2)));
            timer.append(sub4("NAM0", Bytes().zstring("")));
            timer.append(sub4("ALID", Bytes().u32(0)));
            timer.append(sub4("INAM", Bytes().u32(1)));
            timer.append(sub4("SNAM", Bytes().u32(0)));
            timer.append(sub4("ENAM", Bytes().u32(0)));
            timer.append(sub4("SNAM", Bytes().f32(5.f)));
            timer.append(empty("ANAM"));
            b.append(timer);
        }
        b.append(sub4("PNAM", Bytes().u32(quest)));
        b.append(sub4("INAM", Bytes().u32(1)));
        b.append(sub4("VNAM", Bytes().u32(3).u32(3).u32(3).u32(3)));
        return record4("SCEN", 0u, id, b);
    }

    class UnloadedEnv final : public SceneEnvironment
    {
    public:
        bool questRunning(FormId) const override { return true; }
        FormId aliasReference(FormId, std::uint32_t) const override { return kActor; }
        bool evaluate(const std::vector<survey::Ctda>&, FormId, FormId) override { return true; }
        bool resolveLine(FormId, FormId, FormId, SceneLine&) override { return false; }
        bool speak(FormId, const SceneLine&) override { return true; }
        void hold(FormId, bool) override {}
        bool setPackage(FormId, FormId) override { return true; }
        void clearPackage(FormId) override {}
        bool packageFinished(FormId) override { return true; }
        void runFragment(FormId, const std::string&, const std::string&) override {}
        void stopQuest(FormId) override {}
        void log(const std::string&) override {}
        bool actorLoaded(FormId) const override { return false; }
    };

    struct ParsedScenes
    {
        ParsedScenes()
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes records = sceneRecord(kSceneEmpty, kQuestA, false);
            records.append(sceneRecord(kSceneTimer, kQuestA, true));
            bytes.append(group4(Content::fourCC("SCEN"), 0, records));
            Content::Error error;
            SceneRecords parsed;
            mOk = parseSceneRecordsBytes(bytes.str(), "test.esp", order(), SceneLimits(), parsed, error);
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            if (plugin != nullptr)
                mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
        }
        bool mOk = false;
        SceneIndex mIndex;
    };

    TEST(SkyrimBloodIceScene, aSceneWithNoActionsIsRefusedWhenNoActorIsLoadedUnlessTheOptionIsOn)
    {
        const ParsedScenes p;
        ASSERT_TRUE(p.mOk);
        UnloadedEnv env;
        SceneRunner off(p.mIndex, env);
        EXPECT_FALSE(off.start(kSceneEmpty));
        EXPECT_FALSE(off.isPlaying(kSceneEmpty));
        SceneRunner on(p.mIndex, env);
        on.setActionlessStartsUnloaded(true);
        EXPECT_TRUE(on.start(kSceneEmpty));
        EXPECT_TRUE(on.isPlaying(kSceneEmpty));
    }

    TEST(SkyrimBloodIceScene, aSceneWithAnActionStillNeedsALoadedActorWhateverTheOption)
    {
        const ParsedScenes p;
        ASSERT_TRUE(p.mOk);
        UnloadedEnv env;
        SceneRunner on(p.mIndex, env);
        on.setActionlessStartsUnloaded(true);
        EXPECT_FALSE(on.start(kSceneTimer));
    }

    TEST(SkyrimBloodIceScene, aBeginOnQuestStartSceneWithNoActionsBeginsAtQuestStartWithTheOption)
    {
        const ParsedScenes p;
        ASSERT_TRUE(p.mOk);
        UnloadedEnv env;
        SceneRunner on(p.mIndex, env);
        on.setActionlessStartsUnloaded(true);
        EXPECT_EQ(on.questStarted(kQuestA), 1u); // the timer scene is refused, the empty one begins
        EXPECT_TRUE(on.isPlaying(kSceneEmpty));
        EXPECT_FALSE(on.isPlaying(kSceneTimer));
    }

    TEST(SkyrimBloodIceHour, theGameHourGlobalTakesTheCoresHourOnlyWhenTheClockIsValid)
    {
        ASSERT_TRUE(bloodice::gameHourValue(22.5f, true).has_value());
        EXPECT_FLOAT_EQ(*bloodice::gameHourValue(22.5f, true), 22.5f);
        EXPECT_FALSE(bloodice::gameHourValue(22.5f, false).has_value());
        EXPECT_FALSE(bloodice::gameHourValue(24.5f, true).has_value()) << "an hour outside [0, 24) is no hour";
        EXPECT_FALSE(bloodice::gameHourValue(-0.5f, true).has_value());
    }

    TEST(SkyrimBloodIceConstants, theNamesAreTheDatas)
    {
        EXPECT_EQ(bloodice::kGameHourGlobal, 0x38u);
        EXPECT_EQ(bloodice::kMs11Quest, 0x1F7A3u);
        EXPECT_EQ(std::string(bloodice::kKey), "blood on the ice");
    }
}
