// Task 0325 (Dark Brotherhood, the sleep abduction): what DBEntranceQuest and DarkBrotherhood need from the host.
// (1) RegisterForSleep and the events OnSleepStart and OnSleepStop, (2) Form.RegisterForUpdate from one script of a form
// reaches the OnUpdate of ANOTHER script on that form (QF_DarkBrotherhood registers, DarkBrotherhood handles),
// (3) the player's own MoveTo goes to the pack's sink, (4) the travel service keeps a script's move for the global
// script, (5) GetInSameCell (Astrid's forced greeting needs the player in her cell). Generated scripts, no game data.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/map.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/travel.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <span>
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
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kRef = 0x01000A01;
    constexpr FormId kMarker = 0x01000A02;
    constexpr FormId kMover = 0x01000A09;
    constexpr FormId kShack = 0x01000C00;
    constexpr FormId kBase = 0x01000B00;

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
            if (reference != kMarker)
                return false;
            x = 1764.0;
            y = 1049.0;
            z = 7519.0;
            return true;
        }
        bool referenceRotation(FormId reference, double (&xyz)[3]) const override
        {
            if (reference != kMarker)
                return false;
            xyz[0] = 0.0;
            xyz[1] = 0.0;
            xyz[2] = 3.5;
            return true;
        }
        FormId cellOf(FormId reference) const override { return reference == kMarker ? kShack : 0; }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
    };

    class NoHooks final : public PapyrusWorldHooks
    {
    };

    // The entrance quest's script: registers for the sleep and records what it is told.
    ScriptBuilder sleepWatch()
    {
        ScriptBuilder s("SleepWatch");
        s.parent("ObjectReference");
        s.var("slept", "Int");
        s.var("stopped", "Int");
        s.var("interrupted", "Bool");
        s.var("startDay", "Float");
        s.var("endDay", "Float");
        s.function("Arm").local("n", "None").callMethod("RegisterForSleep", Id("self"), Id("n"));
        s.function("Disarm").local("n", "None").callMethod("UnregisterForSleep", Id("self"), Id("n"));
        s.function("OnSleepStart")
            .param("afSleepStartTime", "Float")
            .param("afDesiredSleepEndTime", "Float")
            .iadd(Id("slept"), Id("slept"), Int(1))
            .assign(Id("startDay"), Id("afSleepStartTime"))
            .assign(Id("endDay"), Id("afDesiredSleepEndTime"));
        s.function("OnSleepStop")
            .param("abInterrupted", "Bool")
            .iadd(Id("stopped"), Id("stopped"), Int(1))
            .assign(Id("interrupted"), Id("abInterrupted"));
        return s;
    }

    // Two scripts on one form: Reg registers the update (the quest's fragment script), Tick handles it (the quest's script).
    ScriptBuilder regScript()
    {
        ScriptBuilder s("Reg");
        s.parent("ObjectReference");
        s.function("Go").local("n", "None").callMethod("RegisterForUpdate", Id("self"), Id("n"), { Flt(1.0f) });
        s.function("Stop").local("n", "None").callMethod("UnregisterForUpdate", Id("self"), Id("n"));
        return s;
    }

    ScriptBuilder tickScript()
    {
        ScriptBuilder s("Tick");
        s.parent("ObjectReference");
        s.var("ticks", "Int");
        s.function("OnUpdate").iadd(Id("ticks"), Id("ticks"), Int(1));
        return s;
    }

    // The abduction: Game.GetPlayer().MoveTo(marker)
    ScriptBuilder abductor()
    {
        ScriptBuilder s("Abductor");
        s.parent("ObjectReference");
        s.autoProperty("Marker", "ObjectReference");
        s.function("Go")
            .local("p", "Actor")
            .local("n", "None")
            .callStatic("Game", "GetPlayer", Id("p"))
            .callMethod("MoveTo", Id("p"), Id("n"), { Id("::Marker_var"), Flt(0.0f), Flt(0.0f), Flt(0.0f), Bool(true) });
        return s;
    }

    vmad::Script scriptNamed(const char* name)
    {
        vmad::Script script;
        script.mName = name;
        return script;
    }

    struct Rig
    {
        Rig()
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            form.function("RegisterForSleep").native();
            form.function("UnregisterForSleep").native();
            form.function("RegisterForUpdate").native().param("afInterval", "Float");
            form.function("UnregisterForUpdate").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
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
            mData.add(actor);
            ScriptBuilder game("Game");
            game.function("GetPlayer", "Actor").global().native();
            mData.add(game);
            mData.add(sleepWatch());
            mData.add(regScript());
            mData.add(tickScript());
            mData.add(abductor());
            mData.mClasses[kRef] = "ObjectReference";
            mData.mClasses[kMarker] = "ObjectReference";
            mData.mClasses[kMover] = "ObjectReference";
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
            std::vector<vmad::Script> scripts;
            for (const char* name : { "SleepWatch", "Reg", "Tick" })
                scripts.push_back(scriptNamed(name));
            EXPECT_EQ(mHost->attachReference(kRef, kBase, false, std::move(scripts)), 3u);
            tick();
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 3, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(dt);
        }
        double number(const char* script, const char* name)
        {
            return mHost->numberVariable(kRef, script, name).value_or(-1.0);
        }
        FakeData mData;
        NoHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    // ---- (1) the sleep ----------------------------------------------------------------------------------------------

    TEST(SkyrimDb02Sleep, aScriptThatRegisteredHearsTheSleepWithItsGameDays)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "SleepWatch", "Arm"));
        rig.mHost->onSleepStart(12.25, 12.5);
        rig.mHost->onSleepStop(false);
        rig.tick();
        EXPECT_EQ(rig.number("SleepWatch", "slept"), 1.0);
        EXPECT_EQ(rig.number("SleepWatch", "stopped"), 1.0);
        EXPECT_NEAR(rig.number("SleepWatch", "startDay"), 12.25, 1e-4);
        EXPECT_NEAR(rig.number("SleepWatch", "endDay"), 12.5, 1e-4);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimDb02Sleep, aScriptThatNeverRegisteredHearsNothing)
    {
        Rig rig;
        rig.mHost->onSleepStart(1.0, 1.3);
        rig.mHost->onSleepStop(true);
        rig.tick();
        EXPECT_EQ(rig.number("SleepWatch", "slept"), 0.0);
        EXPECT_EQ(rig.number("SleepWatch", "stopped"), 0.0);
    }

    TEST(SkyrimDb02Sleep, unregisteringStopsTheEvents)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "SleepWatch", "Arm"));
        rig.mHost->onSleepStart(1.0, 1.3);
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "SleepWatch", "Disarm"));
        rig.mHost->onSleepStart(2.0, 2.3);
        rig.tick();
        EXPECT_EQ(rig.number("SleepWatch", "slept"), 1.0);
    }

    TEST(SkyrimDb02Sleep, registeringTwiceStillGivesOneEventPerSleep)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "SleepWatch", "Arm"));
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "SleepWatch", "Arm"));
        rig.mHost->onSleepStart(1.0, 1.3);
        rig.tick();
        EXPECT_EQ(rig.number("SleepWatch", "slept"), 1.0);
    }

    // ---- (2) an update registered for the form --------------------------------------------------------------------

    TEST(SkyrimDb02Update, theUpdateRegisteredByOneScriptReachesTheOtherScriptOfTheForm)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "Reg", "Go"));
        rig.tick(30, 0.1f); // 3 s: the interval is 1 s
        EXPECT_GE(rig.number("Tick", "ticks"), 2.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimDb02Update, unregisteringFromOneScriptEndsItForTheOthersToo)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "Reg", "Go"));
        rig.tick(30, 0.1f);
        ASSERT_TRUE(rig.mHost->callFunction(kRef, "Reg", "Stop"));
        const double before = rig.number("Tick", "ticks");
        rig.tick(40, 0.1f);
        EXPECT_EQ(rig.number("Tick", "ticks"), before);
    }

    // ---- (3) the player's MoveTo -----------------------------------------------------------------------------------

    TEST(SkyrimDb02PlayerMove, theSinkGetsTheTargetsCellPositionAndHeading)
    {
        Rig rig;
        struct Seen
        {
            int mCalls = 0;
            FormId mCell = 0;
            double mAt[3] = { 0, 0, 0 };
            double mHeading = 0;
        } seen;
        rig.mHost->setPlayerMoveSink([&seen](FormId cell, const double* at, double heading) {
            ++seen.mCalls;
            seen.mCell = cell;
            for (int i = 0; i < 3; ++i)
                seen.mAt[i] = at[i];
            seen.mHeading = heading;
            return true;
        });
        vmad::Script script = scriptNamed("Abductor");
        vmad::Property marker;
        marker.mName = "Marker";
        marker.mType = 1;
        marker.mValue.mKind = vmad::ValueKind::Object;
        marker.mValue.mForm = kMarker;
        script.mProperties.push_back(marker);
        std::vector<vmad::Script> scripts;
        scripts.push_back(std::move(script));
        ASSERT_EQ(rig.mHost->attachReference(kMover, kBase, false, std::move(scripts)), 1u);
        ASSERT_TRUE(rig.mHost->callFunction(kMover, "Abductor", "Go"));
        rig.tick();
        EXPECT_EQ(seen.mCalls, 1);
        EXPECT_EQ(seen.mCell, kShack);
        EXPECT_NEAR(seen.mAt[0], 1764.0, 1e-6);
        EXPECT_NEAR(seen.mAt[2], 7519.0, 1e-6);
        EXPECT_NEAR(seen.mHeading, 3.5, 1e-6);
    }

    // ---- (4) the travel service keeps the move for the global script ------------------------------------------------

    struct MoveRig
    {
        MoveRig()
            : mMap(map::Hooks{})
            , mService(mMap, travel::Hooks{})
        {
        }
        map::MapService mMap;
        travel::TravelService mService;
    };

    TEST(SkyrimDb02Travel, aQueuedMoveIsReadAsASequenceAndAPlace)
    {
        MoveRig rig;
        EXPECT_EQ(rig.mService.moveSequence(), 0u);
        EXPECT_TRUE(rig.mService.queueMove("AbandonedShackInterior", 1764.0, 1049.0, 7519.0, 3.5));
        EXPECT_EQ(rig.mService.moveSequence(), 1u);
        EXPECT_EQ(rig.mService.movePlace(), "AbandonedShackInterior;1764.000;1049.000;7519.000;3.50000");
        EXPECT_TRUE(rig.mService.queueMove("AbandonedShackInterior", 1.0, 2.0, 3.0, 0.0));
        EXPECT_EQ(rig.mService.moveSequence(), 2u);
    }

    TEST(SkyrimDb02Travel, anEmptyNameASeparatorOrANonFiniteNumberIsRefused)
    {
        MoveRig rig;
        EXPECT_FALSE(rig.mService.queueMove("", 0, 0, 0, 0));
        EXPECT_FALSE(rig.mService.queueMove("A;B", 0, 0, 0, 0));
        EXPECT_FALSE(rig.mService.queueMove("A", std::nan(""), 0, 0, 0));
        EXPECT_FALSE(rig.mService.queueMove("A", 0, 0, 0, INFINITY));
        EXPECT_EQ(rig.mService.moveSequence(), 0u);
    }

    // ---- (5) GetInSameCell -----------------------------------------------------------------------------------------

    survey::Ctda sameCell(std::uint32_t reference, float value = 1.f)
    {
        survey::Ctda c;
        c.mFunction = 32;
        c.mParam1 = reference;
        c.mCompare = 0;
        c.mComparison = value;
        return c;
    }

    struct SameCellFixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimDb02SameCell, theSpeakerAndThePlayerInOneInteriorShareItsCell)
    {
        SameCellFixture f;
        DialogueSpeaker speaker;
        speaker.mBase = 0x01000777;
        speaker.mCell = 0x00050F1E;
        f.mWorld.setSpeaker(speaker);
        f.mWorld.setPlayerPlace(true, true, 0x00050F1E, kNullFormId);
        EXPECT_TRUE(f.holds(sameCell(0x14)));
    }

    TEST(SkyrimDb02SameCell, anotherCellOrAnUnknownPlaceIsNotTheSameCell)
    {
        SameCellFixture f;
        DialogueSpeaker speaker;
        speaker.mBase = 0x01000777;
        speaker.mCell = 0x00050F1E;
        f.mWorld.setSpeaker(speaker);
        EXPECT_FALSE(f.holds(sameCell(0x14))) << "the player's place is not told";
        f.mWorld.setPlayerPlace(true, true, 0x0001F86D, kNullFormId);
        EXPECT_FALSE(f.holds(sameCell(0x14))) << "the player stands in another cell";
        f.mWorld.setPlayerPlace(true, false, kNullFormId, 0x2A);
        EXPECT_FALSE(f.holds(sameCell(0x14))) << "an exterior has no cell id here";
    }
}
