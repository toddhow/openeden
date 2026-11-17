// Task 0268 (the main quest's opening at Helgen): a script's MoveTo that the world cannot do in place (an actor, an
// object of another cell) near the player makes a NEW reference there (PapyrusWorldHooks::relocateObject), hides the old
// one and points the quest's aliases at the new one; ObjectReference.Is3DLoaded asks the world; the alias cell-load
// replay reaches an alias script; a begin-on-quest-start scene waits for its actors when asked. Generated scripts, a
// fake game and fake hooks; no game data. Written AFTER the code: mutation checks instead of a red run against stubs
// (see docs/tasks/0268-skyrim-helgen.md).
#include <game_skyrimse/main_quest.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/scene.hpp>
#include <game_skyrimse/scene_runner.hpp>
#include <game_skyrimse/vmad.hpp>

#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
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
    using openeden::sdk::game::kCreatedObjectBase;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kActor = 0x01000A01; // the reference the script runs on (an actor in a far cell)
    constexpr FormId kMarker = 0x01000A02; // where it is moved
    constexpr FormId kBaseActor = 0x01000B01;
    constexpr FormId kBaseHost = 0x01000B00;
    constexpr FormId kCellFar = 0x01000C01;
    constexpr FormId kCellNear = 0x01000C02;

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
        FormId baseOf(FormId reference) const override
        {
            const auto it = mBases.find(reference);
            return it != mBases.end() ? it->second : 0;
        }
        FormId cellOf(FormId reference) const override
        {
            const auto it = mCellOf.find(reference);
            return it != mCellOf.end() ? it->second : 0;
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
        std::map<FormId, FormId> mBases;
        std::map<FormId, FormId> mCellOf;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        struct Relocation
        {
            FormId mReference;
            FormId mBase;
            ObjectMoveRequest mRequest;
            FormId mCell;
        };
        bool setObjectEnabled(FormId reference, bool enabled, float) override
        {
            mEnables.emplace_back(reference, enabled);
            return true;
        }
        bool moveObject(FormId reference, const ObjectMoveRequest&) override
        {
            mMoves.push_back(reference);
            return true;
        }
        ObjectPlaceResult relocateObject(
            FormId reference, FormId base, const ObjectMoveRequest& request, FormId destinationCell) override
        {
            mRelocations.push_back({ reference, base, request, destinationCell });
            if (!mCanRelocate)
                return {};
            return { static_cast<FormId>(kCreatedObjectBase + mRelocations.size()), destinationCell };
        }
        bool referenceLoaded(FormId reference) override { return mLoaded.count(reference) != 0; }
        std::vector<std::pair<FormId, bool>> mEnables;
        std::vector<FormId> mMoves;
        std::vector<Relocation> mRelocations;
        std::set<FormId> mLoaded;
        bool mCanRelocate = true;
    };

    ScriptBuilder probeScript()
    {
        ScriptBuilder s("Probe");
        s.parent("ObjectReference");
        s.autoProperty("Marker", "ObjectReference");
        s.var("flag", "Bool");
        s.function("GoToMarker")
            .local("n", "None")
            .callMethod("MoveTo", Id("self"), Id("n"), { Id("::Marker_var"), Flt(5.f), Flt(0.f), Flt(0.f), Bool(false) });
        s.function("AskLoaded")
            .local("v", "Bool")
            .callMethod("Is3DLoaded", Id("self"), Id("v"))
            .assign(Id("flag"), Id("v"));
        return s;
    }

    vmad::Script probeOn()
    {
        vmad::Script s;
        s.mName = "Probe";
        vmad::Property p;
        p.mName = "Marker";
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = kMarker;
        p.mValue.mAlias = -1;
        s.mProperties = { p };
        return s;
    }

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("MoveTo")
            .native()
            .param("akTarget", "ObjectReference")
            .param("afXOffset", "Float")
            .param("afYOffset", "Float")
            .param("afZOffset", "Float")
            .param("abMatchRotation", "Bool");
        ref.function("Is3DLoaded", "Bool").native();
        data.add(ref);
    }

    // A quest with one reference alias filled with kActor (ALFR).
    Bytes questWithAlias()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes quest;
        quest.append(sub4("EDID", Bytes().zstring("Q1")));
        quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        quest.append(sub4("NEXT", Bytes()));
        quest.append(sub4("ALST", Bytes().u32(1)));
        quest.append(sub4("ALID", Bytes().zstring("Actor")));
        quest.append(sub4("FNAM", Bytes().u32(0)));
        quest.append(sub4("ALFR", Bytes().u32(kActor)));
        quest.append(sub4("VTCK", Bytes().u32(0)));
        quest.append(sub4("ALED", Bytes()));
        bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
        return bytes;
    }

    struct Rig
    {
        Rig()
        {
            addClasses(mData);
            mData.add(probeScript());
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kActor] = "ObjectReference";
            mData.mClasses[kMarker] = "ObjectReference";
            mData.mClasses[kBaseActor] = "Form";
            mData.mPositions[kActor] = { 50000.0, 50000.0, 0.0 };
            mData.mPositions[kMarker] = { 1000.0, 2000.0, 300.0 };
            mData.mBases[kActor] = kBaseActor;
            mData.mCellOf[kActor] = kCellFar;
            mData.mCellOf[kMarker] = kCellNear;
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            const Bytes bytes = questWithAlias();
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            EXPECT_EQ(mHost->attachReference(kActor, kBaseHost, false, { probeOn() }), 1u);
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
        bool call(const char* function)
        {
            const bool ok = mHost->callFunction(kActor, "Probe", function);
            tick();
            return ok;
        }
        void playerAt(double x, double y)
        {
            PapyrusPlayerView view;
            view.mKnown = true;
            view.mX = x;
            view.mY = y;
            mHost->setPlayerView(view);
        }
        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimMainQuestRelocation, aMoveToIsNotRelocatedUnlessTheSettingIsOn)
    {
        Rig rig;
        rig.playerAt(1000.0, 2000.0);
        ASSERT_TRUE(rig.call("GoToMarker"));
        EXPECT_TRUE(rig.mHooks.mRelocations.empty());
        EXPECT_EQ(rig.mHooks.mMoves.size(), 1u) << "the move goes the old way";
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 1).mReference, kActor);
    }

    TEST(SkyrimMainQuestRelocation, aMoveToNearThePlayerMakesANewReferenceHidesTheOldAndTheAliasFollows)
    {
        Rig rig;
        rig.mHost->setRelocation(true, 7000.0);
        rig.playerAt(1000.0, 2000.0);
        ASSERT_TRUE(rig.call("GoToMarker"));
        ASSERT_EQ(rig.mHooks.mRelocations.size(), 1u);
        const FakeHooks::Relocation& r = rig.mHooks.mRelocations[0];
        EXPECT_EQ(r.mReference, kActor);
        EXPECT_EQ(r.mBase, kBaseActor);
        EXPECT_EQ(r.mCell, kCellNear) << "the cell of the marker it goes to";
        ASSERT_TRUE(r.mRequest.hasPosition);
        EXPECT_EQ(r.mRequest.position[0], 1005.0) << "the marker's place plus the offsets";
        EXPECT_EQ(r.mRequest.position[1], 2000.0);
        EXPECT_EQ(r.mRequest.position[2], 300.0);
        EXPECT_TRUE(rig.mHooks.mMoves.empty()) << "nothing was moved in place";
        ASSERT_EQ(rig.mHooks.mEnables.size(), 1u);
        EXPECT_EQ(rig.mHooks.mEnables[0].first, kActor);
        EXPECT_FALSE(rig.mHooks.mEnables[0].second) << "the old reference is hidden";
        const FormId made = static_cast<FormId>(kCreatedObjectBase + 1);
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 1).mReference, made);
        const auto at = rig.mHost->positionOverride(made);
        ASSERT_TRUE(at.has_value());
        EXPECT_EQ((*at)[0], 1005.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimMainQuestRelocation, aMoveToFarFromThePlayerIsNotRelocated)
    {
        Rig rig;
        rig.mHost->setRelocation(true, 7000.0);
        rig.playerAt(40000.0, 40000.0);
        ASSERT_TRUE(rig.call("GoToMarker"));
        EXPECT_TRUE(rig.mHooks.mRelocations.empty());
        EXPECT_EQ(rig.mHooks.mMoves.size(), 1u);
    }

    TEST(SkyrimMainQuestRelocation, aMoveToWithNoKnownPlayerIsNotRelocated)
    {
        Rig rig;
        rig.mHost->setRelocation(true, 7000.0);
        ASSERT_TRUE(rig.call("GoToMarker"));
        EXPECT_TRUE(rig.mHooks.mRelocations.empty());
    }

    TEST(SkyrimMainQuestRelocation, aWorldThatRefusesLeavesTheOldPathAndTheAliasAlone)
    {
        Rig rig;
        rig.mHost->setRelocation(true, 7000.0);
        rig.mHooks.mCanRelocate = false;
        rig.playerAt(1000.0, 2000.0);
        ASSERT_TRUE(rig.call("GoToMarker"));
        EXPECT_EQ(rig.mHooks.mRelocations.size(), 1u);
        EXPECT_EQ(rig.mHooks.mMoves.size(), 1u) << "the old path ran";
        EXPECT_TRUE(rig.mHooks.mEnables.empty());
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 1).mReference, kActor);
    }

    TEST(SkyrimMainQuestRelocation, aSecondMoveOfTheOldReferenceMovesTheNewOne)
    {
        Rig rig;
        rig.mHost->setRelocation(true, 7000.0);
        rig.playerAt(1000.0, 2000.0);
        ASSERT_TRUE(rig.call("GoToMarker"));
        ASSERT_TRUE(rig.call("GoToMarker"));
        ASSERT_EQ(rig.mHooks.mRelocations.size(), 2u);
        EXPECT_EQ(rig.mHooks.mRelocations[1].mReference, static_cast<FormId>(kCreatedObjectBase + 1))
            << "the script's old reference stands for the new one";
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 1).mReference, static_cast<FormId>(kCreatedObjectBase + 2));
    }

    TEST(SkyrimMainQuestRelocation, replaceReferenceChangesEveryAliasThatHoldsItAndNothingElse)
    {
        Rig rig;
        EXPECT_EQ(rig.mState.replaceReference(kActor, 0x77), 1u);
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 1).mReference, 0x77u);
        EXPECT_EQ(rig.mState.replaceReference(kActor, 0x78), 0u) << "nothing holds it any more";
        EXPECT_EQ(rig.mState.replaceReference(0, 0x78), 0u);
        EXPECT_EQ(rig.mState.replaceReference(0x77, 0x77), 0u);
    }

    TEST(SkyrimMainQuestRelocation, is3DLoadedAsksTheWorld)
    {
        Rig rig;
        rig.mHost->setRelocation(true, 7000.0);
        ASSERT_TRUE(rig.call("AskLoaded"));
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "flag").value_or(-1.0), 0.0);
        rig.mHooks.mLoaded.insert(kActor);
        ASSERT_TRUE(rig.call("AskLoaded"));
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "flag").value_or(-1.0), 1.0);
    }

    // ---- the scene that begins on quest start waits for its actors -----------------------------------------------

    Bytes empty(const char (&type)[5]) { return sub4(type, Bytes()); }

    Bytes sceneRecord(FormId id, FormId quest)
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
        b.append(sub4("PNAM", Bytes().u32(quest)));
        b.append(sub4("INAM", Bytes().u32(1)));
        b.append(sub4("VNAM", Bytes().u32(3).u32(3).u32(3).u32(3)));
        return record4("SCEN", 0u, id, b);
    }

    class WaitingEnv final : public SceneEnvironment
    {
    public:
        bool questRunning(FormId) const override { return mRunning; }
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
        bool actorLoaded(FormId) const override { return mLoaded; }
        bool mRunning = true;
        bool mLoaded = false;
    };

    struct ParsedScene
    {
        ParsedScene()
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("SCEN"), 0, sceneRecord(0x01000900, kQuestA)));
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

    TEST(SkyrimMainQuestScene, aBeginOnQuestStartSceneIsDroppedWhenItsActorsAreNotLoadedUnlessAskedToWait)
    {
        const ParsedScene p;
        ASSERT_TRUE(p.mOk);
        WaitingEnv env;
        SceneRunner runner(p.mIndex, env);
        EXPECT_EQ(runner.questStarted(kQuestA), 0u);
        env.mLoaded = true;
        for (int i = 0; i < 5; ++i)
            runner.update(1.f);
        EXPECT_FALSE(runner.isPlaying(0x01000900)) << "dropped: nothing asks again";
    }

    TEST(SkyrimMainQuestScene, aWaitingSceneBeginsWhenItsActorsLoadAndNotBefore)
    {
        const ParsedScene p;
        ASSERT_TRUE(p.mOk);
        WaitingEnv env;
        SceneRunner runner(p.mIndex, env);
        runner.setRetryQuestStart(true);
        EXPECT_EQ(runner.questStarted(kQuestA), 0u);
        runner.update(1.1f);
        runner.update(1.1f);
        EXPECT_FALSE(runner.isPlaying(0x01000900));
        env.mLoaded = true;
        runner.update(0.5f);
        EXPECT_FALSE(runner.isPlaying(0x01000900)) << "it asks once a second";
        runner.update(0.6f);
        EXPECT_TRUE(runner.isPlaying(0x01000900));
    }

    TEST(SkyrimMainQuestScene, aWaitingSceneIsForgottenWhenItsQuestStops)
    {
        const ParsedScene p;
        ASSERT_TRUE(p.mOk);
        WaitingEnv env;
        SceneRunner runner(p.mIndex, env);
        runner.setRetryQuestStart(true);
        EXPECT_EQ(runner.questStarted(kQuestA), 0u);
        env.mRunning = false;
        runner.update(1.1f);
        env.mRunning = true;
        env.mLoaded = true;
        runner.update(1.1f);
        EXPECT_FALSE(runner.isPlaying(0x01000900));
    }

    // ---- the constants ------------------------------------------------------------------------------------------

    TEST(SkyrimMainQuestStart, theQuestIsUnboundAndTheStartIsInTamriel)
    {
        EXPECT_EQ(mainquest::kUnboundQuest, 0x0003372Bu);
        EXPECT_EQ(std::string(mainquest::kHelgenStart).rfind("@Tamriel,", 0), 0u);
        EXPECT_EQ(std::string(mainquest::kMainQuestStartKey), "main quest start");
        EXPECT_GT(mainquest::kRelocateRange, 0.0);
    }
}
