// Task 0350 (Skyrim Daedric quest "Ill Met by Moonlight", DA05): a scene that has a begin fragment starts although none of
// its actors is loaded, with the option (the fragment moves them: SF_DA05FuneralScene), and is refused without it.
// Generated records and a fake environment; no game data. Written first and run red against a stub option.
#include <game_skyrimse/papyrus_world.hpp>
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
    constexpr FormId kSceneFrag = 0x01000900;
    constexpr FormId kSceneBare = 0x01000901;
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

    void wstr(Bytes& b, const std::string& s)
    {
        b.u16(static_cast<std::uint16_t>(s.size()));
        for (char c : s)
            b.u8(static_cast<std::uint8_t>(c));
    }

    Bytes beginVmad()
    {
        Bytes b;
        b.u16(5).u16(2).u16(0);
        b.u8(2).u8(1);
        wstr(b, "SF_Test_00000900");
        b.u8(1);
        wstr(b, "SF_Test_00000900");
        wstr(b, "Fragment_0");
        b.u16(0);
        return b;
    }

    // one phase, one actor entry (alias 1), one timer action; optionally a begin fragment
    Bytes sceneRecord(FormId id, bool withFragment)
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
        if (withFragment)
            b.append(sub4("VMAD", beginVmad()));
        b.append(sub4("FNAM", Bytes().u32(0)));
        b.append(phase);
        b.append(sub4("ALID", Bytes().u32(1)));
        b.append(sub4("LNAM", Bytes().u32(0)));
        b.append(sub4("DNAM", Bytes().u32(26)));
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
        b.append(sub4("PNAM", Bytes().u32(kQuestA)));
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
        void runFragment(FormId, const std::string&, const std::string& fn) override { mFragments.push_back(fn); }
        void stopQuest(FormId) override {}
        void log(const std::string&) override {}
        bool actorLoaded(FormId) const override { return false; }
        std::vector<std::string> mFragments;
    };

    struct ParsedScenes
    {
        ParsedScenes()
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes records = sceneRecord(kSceneFrag, true);
            records.append(sceneRecord(kSceneBare, false));
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

    TEST(SkyrimMoonlightScene, aSceneWithABeginFragmentIsRefusedWhenNoActorIsLoadedUnlessTheOptionIsOn)
    {
        const ParsedScenes p;
        ASSERT_TRUE(p.mOk);
        UnloadedEnv env;
        SceneRunner off(p.mIndex, env);
        EXPECT_FALSE(off.start(kSceneFrag));
        SceneRunner on(p.mIndex, env);
        on.setBeginFragmentStartsUnloaded(true);
        EXPECT_TRUE(on.start(kSceneFrag));
        EXPECT_TRUE(on.isPlaying(kSceneFrag));
        ASSERT_EQ(env.mFragments.size(), 1u);
        EXPECT_EQ(env.mFragments[0], "Fragment_0");
    }

    TEST(SkyrimMoonlightScene, aSceneWithoutABeginFragmentStillNeedsALoadedActorWhateverTheOption)
    {
        const ParsedScenes p;
        ASSERT_TRUE(p.mOk);
        UnloadedEnv env;
        SceneRunner on(p.mIndex, env);
        on.setBeginFragmentStartsUnloaded(true);
        EXPECT_FALSE(on.start(kSceneBare));
        EXPECT_FALSE(on.isPlaying(kSceneBare));
    }

    TEST(SkyrimMoonlightRace, aRaceASetRaceGivesReachesThePackThroughTheHooks)
    {
        PackWorldHooks refusing{ PackWorldEnv() };
        EXPECT_FALSE(refusing.setActorRace(0x10, 0x20));
        std::vector<std::pair<FormId, FormId>> seen;
        PackWorldEnv env;
        env.setActorRace = [&seen](FormId reference, FormId race) {
            seen.emplace_back(reference, race);
            return true;
        };
        PackWorldHooks hooks(std::move(env));
        EXPECT_TRUE(hooks.setActorRace(0x10, 0x20));
        ASSERT_EQ(seen.size(), 1u);
        EXPECT_EQ(seen[0].first, 0x10u);
        EXPECT_EQ(seen[0].second, 0x20u);
    }
}
