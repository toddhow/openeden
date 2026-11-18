// Task 0313 (Dragon Rising): the game's own dragon death sequence finishes. A script's GetDistance on a dragon read the
// data's placement (1,774 units up) instead of where the dragon fell, so `dragonActorSCRIPT`'s wait never ended and
// MQ104 never reached stage 90. Here: the host asks the world for an actor's live place first; the player's soul count
// (`ModActorValue("dragonsouls")`) is the pack's number when it knows it; `hasScript` names a dragon's own script; and
// the live places a pack watches. Generated scripts, a fake game and fake hooks. Written first and run red against the
// older host (stubs).
#include <game_skyrimse/live_places.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <openeden/game/actor_rules.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
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
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kDragon = 0x01000A01;
    constexpr FormId kPlayer = 0x14;
    constexpr FormId kBaseHost = 0x01000B00;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

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
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        bool referencePosition(FormId reference, double& x, double& y, double& z) const override
        {
            const auto it = mPositions.find(reference);
            if (it == mPositions.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
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
        std::map<FormId, std::array<double, 3>> mPositions;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        bool livePosition(FormId reference, double& x, double& y, double& z) override
        {
            ++mLiveAsks;
            const auto it = mLive.find(reference);
            if (it == mLive.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        bool moveObject(FormId, const ObjectMoveRequest&) override { return true; }
        bool actorValue(FormId actor, std::string_view name, double& current, double& maximum) override
        {
            if (!mKnowsSouls || actor != kPlayer || name != "dragonsouls")
                return false;
            current = mSouls;
            maximum = 0.0;
            return true;
        }
        void actorValueSet(FormId actor, std::string_view name, double value) override
        {
            mSets.push_back(std::string(name) + "=" + std::to_string(static_cast<int>(value)));
            if (actor == kPlayer && name == "dragonsouls" && mKnowsSouls)
                mSouls = value; // the pack keeps the number itself
        }
        std::map<FormId, std::array<double, 3>> mLive;
        std::vector<std::string> mSets;
        int mLiveAsks = 0;
        bool mKnowsSouls = true;
        double mSouls = 2.0;
    };

    ScriptBuilder probeScript()
    {
        ScriptBuilder s("Probe");
        s.parent("ObjectReference");
        s.autoProperty("Target", "ObjectReference");
        s.var("distance", "Float");
        s.var("souls", "Float");
        s.function("Measure")
            .local("d", "Float")
            .callMethod("GetDistance", Id("self"), Id("d"), { Id("::Target_var") })
            .assign(Id("distance"), Id("d"));
        s.function("Move")
            .local("n", "None")
            .callMethod(
                "MoveTo", Id("self"), Id("n"), { Id("::Target_var"), Flt(5.f), Flt(0.f), Flt(0.f), Bool(false) });
        s.function("Absorb")
            .local("n", "None")
            .callMethod("ModActorValue", Id("::Target_var"), Id("n"), { Str("dragonsouls"), Flt(1.f) });
        s.function("ReadSouls")
            .local("v", "Float")
            .callMethod("GetActorValue", Id("::Target_var"), Id("v"), { Str("dragonsouls") })
            .assign(Id("souls"), Id("v"));
        return s;
    }

    vmad::Script probeOn()
    {
        vmad::Script s;
        s.mName = "Probe";
        vmad::Property p;
        p.mName = "Target";
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = kPlayer;
        p.mValue.mAlias = -1;
        s.mProperties = { p };
        return s;
    }

    struct Rig
    {
        Rig()
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            ref.function("GetDistance", "Float").native().param("akOther", "ObjectReference");
            ref.function("MoveTo")
                .native()
                .param("akTarget", "ObjectReference")
                .param("afXOffset", "Float")
                .param("afYOffset", "Float")
                .param("afZOffset", "Float")
                .param("abMatchRotation", "Bool");
            mData.add(ref);
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            actor.function("ModActorValue").native().param("asValueName", "String").param("afAmount", "Float");
            actor.function("GetActorValue", "Float").native().param("asValueName", "String");
            mData.add(actor);
            mData.add(probeScript());
            mData.mClasses[kDragon] = "Actor";
            mData.mClasses[kPlayer] = "Actor";
            mData.mPositions[kDragon] = { 0.0, 0.0, 1774.0 }; // where the data placed it
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            PapyrusPlayerView view;
            view.mKnown = true;
            view.mX = 0.0;
            view.mY = 0.0;
            view.mZ = 0.0;
            mHost->setPlayerView(view);
            EXPECT_EQ(mHost->attachReference(kDragon, kBaseHost, false, { probeOn() }), 1u);
            tick();
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        void call(const char* function)
        {
            EXPECT_TRUE(mHost->callFunction(kDragon, "Probe", function));
            tick();
        }
        double number(const char* name) { return mHost->numberVariable(kDragon, "Probe", name).value_or(-1.0); }
        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    // ---- where an actor is ----------------------------------------------------------------------------------------

    TEST(SkyrimDragonRising, withNoLivePlaceTheDistanceIsTheDataPlacementsAsBefore)
    {
        Rig rig;
        rig.call("Measure");
        EXPECT_NEAR(rig.number("distance"), 1774.0, 0.01) << "this is what kept the death script waiting";
    }

    TEST(SkyrimDragonRising, aLivePlaceOfTheDragonIsWhereItIsMeasuredFrom)
    {
        Rig rig;
        rig.mHooks.mLive[kDragon] = { 120.0, 0.0, 0.0 }; // where it fell
        rig.call("Measure");
        EXPECT_NEAR(rig.number("distance"), 120.0, 0.01);
        EXPECT_GE(rig.mHooks.mLiveAsks, 1);
    }

    TEST(SkyrimDragonRising, aPlaceAScriptMovedItToStillWinsOverTheLiveOne)
    {
        Rig rig;
        rig.mHooks.mLive[kDragon] = { 120.0, 0.0, 0.0 };
        rig.call("Move"); // MoveTo(the player at 0,0,0) + 5 on x
        rig.call("Measure");
        EXPECT_NEAR(rig.number("distance"), 5.0, 0.01);
    }

    // ---- the player's souls ---------------------------------------------------------------------------------------

    TEST(SkyrimDragonRising, aScriptsSoulReachesThePacksOwnNumberAndNothingStaysInTheHost)
    {
        Rig rig;
        rig.mHooks.mSouls = 2.0;
        rig.call("Absorb");
        ASSERT_EQ(rig.mHooks.mSets.size(), 1u);
        EXPECT_EQ(rig.mHooks.mSets[0], "dragonsouls=3");
        EXPECT_EQ(rig.mHooks.mSouls, 3.0);
        rig.call("ReadSouls");
        EXPECT_EQ(rig.number("souls"), 3.0) << "read back from the pack, not 3 + an offset the host kept";
        rig.call("Absorb");
        rig.call("ReadSouls");
        EXPECT_EQ(rig.number("souls"), 4.0);
    }

    TEST(SkyrimDragonRising, aPackThatKnowsNoSoulCountKeepsTheOlderBehaviour)
    {
        Rig rig;
        rig.mHooks.mKnowsSouls = false;
        rig.call("Absorb");
        rig.call("ReadSouls");
        EXPECT_EQ(rig.number("souls"), 1.0) << "the host's own offset, as before";
    }

    // ---- a dragon's own script ------------------------------------------------------------------------------------

    TEST(SkyrimDragonRising, theHostSaysWhichScriptsAReferenceCarries)
    {
        Rig rig;
        EXPECT_TRUE(rig.mHost->hasScript(kDragon, "Probe"));
        EXPECT_TRUE(rig.mHost->hasScript(kDragon, "probe"));
        EXPECT_FALSE(rig.mHost->hasScript(kDragon, "dragonactorscript"));
        EXPECT_FALSE(rig.mHost->hasScript(0x01000A99, "Probe"));
    }

    // ---- the places a pack watches --------------------------------------------------------------------------------

    class PlaceWriter final : public openeden::sdk::game::ActorRequestWriter
    {
    public:
        bool setMovement(openeden::sdk::game::ActorHandle, float, float) override { return true; }
        bool goTo(openeden::sdk::game::ActorHandle, const openeden::sdk::game::ActorGoal&) override { return true; }
        bool cancelGoal(openeden::sdk::game::ActorHandle) override { return true; }
        bool actorPosition(openeden::sdk::game::ActorHandle h, double& x, double& y, double& z) const override
        {
            const auto it = mPositions.find(h.reference);
            if (it == mPositions.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        std::map<openeden::sdk::game::WorldId, std::array<double, 3>> mPositions;
    };

    openeden::sdk::game::ActorHandle handleOf(openeden::sdk::game::WorldId reference)
    {
        return reference >= 100 ? openeden::sdk::game::ActorHandle{ reference, 1 } : openeden::sdk::game::ActorHandle{};
    }

    TEST(SkyrimLivePlaces, itIsOffByDefaultAndAnswersNothing)
    {
        LivePlaces places;
        PlaceWriter writer;
        writer.mPositions[100] = { 1.0, 2.0, 3.0 };
        double x = 0, y = 0, z = 0;
        EXPECT_FALSE(places.placeOf(100, x, y, z));
        places.update(handleOf, writer);
        EXPECT_FALSE(places.placeOf(100, x, y, z));
        EXPECT_EQ(places.watched(), 0u);
    }

    TEST(SkyrimLivePlaces, theFirstAskWatchesAndTheNextUpdateReadsThePlace)
    {
        LivePlaces places;
        places.setOn(true);
        PlaceWriter writer;
        writer.mPositions[100] = { 1.0, 2.0, 3.0 };
        double x = 0, y = 0, z = 0;
        EXPECT_FALSE(places.placeOf(100, x, y, z)) << "not read yet: the older answer is used meanwhile";
        EXPECT_EQ(places.watched(), 1u);
        places.update(handleOf, writer);
        ASSERT_TRUE(places.placeOf(100, x, y, z));
        EXPECT_EQ(x, 1.0);
        EXPECT_EQ(y, 2.0);
        EXPECT_EQ(z, 3.0);
        writer.mPositions[100] = { 4.0, 5.0, 6.0 };
        places.update(handleOf, writer);
        ASSERT_TRUE(places.placeOf(100, x, y, z));
        EXPECT_EQ(z, 6.0) << "it follows the actor";
    }

    TEST(SkyrimLivePlaces, anActorTheCoreCannotPlaceKeepsItsLastPlaceAndANonActorHasNone)
    {
        LivePlaces places;
        places.setOn(true);
        PlaceWriter writer;
        writer.mPositions[100] = { 1.0, 2.0, 3.0 };
        double x = 0, y = 0, z = 0;
        (void)places.placeOf(100, x, y, z);
        (void)places.placeOf(7, x, y, z); // a reference that is not an actor
        places.update(handleOf, writer);
        writer.mPositions.clear(); // unloaded for a moment
        places.update(handleOf, writer);
        EXPECT_TRUE(places.placeOf(100, x, y, z));
        EXPECT_EQ(x, 1.0);
        EXPECT_FALSE(places.placeOf(7, x, y, z));
    }

    TEST(SkyrimLivePlaces, atMostSixtyFourReferencesAreWatched)
    {
        LivePlaces places;
        places.setOn(true);
        double x = 0, y = 0, z = 0;
        for (openeden::sdk::game::WorldId r = 100; r < 200; ++r)
            (void)places.placeOf(r, x, y, z);
        EXPECT_EQ(places.watched(), LivePlaces::kMaxWatched);
    }
}
