// Task 0306: the start of the Dragonborn questline. The cultist ambush quest creates its actors and the note at the
// location's centre marker: a quest alias that creates a reference (ALCO, ALCA, ALCL) gets one when the quest starts, and
// the alias holds it. Generated records, a fake game and fake hooks; no game data. Written first and run red against stubs.
#include <game_skyrimse/dragonborn.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
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
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kMarker = 0x01000A01; // the placed reference the alias "Marker" is
    constexpr FormId kCultistBase = 0x01000B01;
    constexpr FormId kNoteBase = 0x01000B02;
    constexpr FormId kCellTown = 0x01000C01;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes alias(std::uint32_t id, const std::string& name, const Bytes& fill)
    {
        Bytes b;
        b.append(sub4("ALST", Bytes().u32(id)));
        b.append(sub4("ALID", Bytes().zstring(name)));
        b.append(sub4("FNAM", Bytes().u32(0)));
        b.append(fill);
        b.append(sub4("VTCK", Bytes().u32(0)));
        b.append(sub4("ALED", Bytes()));
        return b;
    }

    Bytes created(FormId base, std::uint32_t at, std::uint32_t level = 0)
    {
        Bytes b = sub4("ALCO", Bytes().u32(base));
        b.append(sub4("ALCA", Bytes().u32(at)));
        b.append(sub4("ALCL", Bytes().u32(level)));
        return b;
    }

    // alias 1 = the marker (a specific reference), 2 = a cultist created at it, 3 = the note created at it, 4 = a
    // cultist created at alias 9, which nothing fills, 5 = a creation with no place (in a container)
    Bytes questBytes(std::uint16_t flags)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes quest;
        quest.append(sub4("EDID", Bytes().zstring("Ambush")));
        quest.append(sub4("DNAM", Bytes().u16(flags).u8(30).u8(0).u32(0).u32(6)));
        quest.append(sub4("NEXT", Bytes()));
        quest.append(sub4("ANAM", Bytes().u32(10)));
        quest.append(alias(1, "Marker", sub4("ALFR", Bytes().u32(kMarker))));
        quest.append(alias(2, "Cultist", created(kCultistBase, 1, 0)));
        quest.append(alias(3, "Note", created(kNoteBase, 1, 3)));
        quest.append(alias(4, "Lost", created(kCultistBase, 9, 0)));
        quest.append(alias(5, "Carried", created(kNoteBase, kNoCreateAt, 0)));
        bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, quest)));
        return bytes;
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
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
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
        std::map<FormId, std::string> mClasses;
        std::map<FormId, std::array<double, 3>> mPositions;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        ObjectPlaceResult placeObject(const ObjectPlaceRequest& request) override
        {
            mPlaces.push_back(request);
            if (!mCanPlace)
                return {};
            return { static_cast<FormId>(kCreatedObjectBase + mPlaces.size()), kCellTown };
        }
        std::vector<ObjectPlaceRequest> mPlaces;
        bool mCanPlace = true;
    };

    // the Cultists' Orders' own script in miniature: it hears of a new container and of being read
    ScriptBuilder noteScript()
    {
        ScriptBuilder s("NoteScript");
        s.parent("ObjectReference");
        s.var("changed", "Int");
        s.var("read", "Int");
        s.function("OnContainerChanged")
            .param("akNewContainer", "ObjectReference")
            .param("akOldContainer", "ObjectReference")
            .iadd(Id("changed"), Id("changed"), Int(1));
        s.function("OnRead").iadd(Id("read"), Id("read"), Int(1));
        return s;
    }

    vmad::Script noteOn()
    {
        vmad::Script s;
        s.mName = "NoteScript";
        return s;
    }

    struct Rig
    {
        explicit Rig(std::uint16_t flags = 0)
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            mData.add(ref);
            mData.add(noteScript());
            mData.mClasses[kQuest] = "Quest";
            mData.mClasses[kMarker] = "ObjectReference";
            mData.mPositions[kMarker] = { 1000.0, 2000.0, 30.0 };
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            const Bytes bytes = questBytes(flags);
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            mRecords = parsed;
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        std::optional<double> number(FormId ref, const char* name)
        {
            return mHost->numberVariable(ref, "NoteScript", name);
        }
        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        QuestRecords mRecords;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    // ---- the parser -------------------------------------------------------------------------------------------

    TEST(SkyrimDragonbornParse, aCreatedAliasKeepsItsObjectItsPlaceAndItsLevel)
    {
        Rig rig;
        ASSERT_EQ(rig.mRecords.mQuests.size(), 1u);
        const QuestRecord& q = rig.mRecords.mQuests[0];
        ASSERT_EQ(q.mAliases.size(), 5u);
        EXPECT_EQ(q.mAliases[1].mFill, AliasFillKind::CreatedObject);
        EXPECT_EQ(q.mAliases[1].mCreateObject, kCultistBase);
        EXPECT_EQ(q.mAliases[1].mCreateAt, 1u);
        EXPECT_EQ(q.mAliases[1].mCreateLevel, 0u);
        EXPECT_EQ(q.mAliases[2].mCreateObject, kNoteBase);
        EXPECT_EQ(q.mAliases[2].mCreateLevel, 3u);
        EXPECT_EQ(q.mAliases[4].mCreateAt, kNoCreateAt);
        EXPECT_EQ(q.mAliases[0].mCreateObject, kNullFormId) << "an ordinary alias creates nothing";
        EXPECT_EQ(q.mAliases[0].mCreateAt, kNoCreateAt);
    }

    // ---- the host creates them when the quest starts ------------------------------------------------------------

    TEST(SkyrimDragonbornCreate, aStartedQuestCreatesAtTheAliasItNamesAndTheAliasHoldsTheNewReference)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        ASSERT_EQ(rig.mHooks.mPlaces.size(), 2u) << "the cultist and the note; not the one with a missing place";
        EXPECT_EQ(rig.mHooks.mPlaces[0].base, kCultistBase);
        EXPECT_EQ(rig.mHooks.mPlaces[0].nearReference, kMarker);
        EXPECT_EQ(rig.mHooks.mPlaces[0].position[0], 1000.0) << "the first stands exactly at the marker";
        EXPECT_EQ(rig.mHooks.mPlaces[1].base, kNoteBase);
        const double dx = rig.mHooks.mPlaces[1].position[0] - 1000.0;
        const double dy = rig.mHooks.mPlaces[1].position[1] - 2000.0;
        EXPECT_GT(std::sqrt(dx * dx + dy * dy), 60.0) << "the second is not on top of the first (two capsules)";
        EXPECT_LT(std::sqrt(dx * dx + dy * dy), 400.0);
        const AliasFill cultist = rig.mState.aliasFill(kQuest, 2);
        EXPECT_TRUE(cultist.mKnown);
        EXPECT_EQ(cultist.mReference, static_cast<FormId>(kCreatedObjectBase + 1));
        EXPECT_EQ(rig.mState.aliasFill(kQuest, 3).mReference, static_cast<FormId>(kCreatedObjectBase + 2));
        EXPECT_EQ(rig.mState.aliasFill(kQuest, 4).mReference, kNullFormId);
        EXPECT_EQ(rig.mState.aliasFill(kQuest, 5).mReference, kNullFormId);
        ASSERT_EQ(rig.mHost->createdObjects().size(), 2u) << "the host keeps them like a PlaceAtMe one";
        EXPECT_EQ(rig.mHost->createdObjects()[0].mBase, kCultistBase);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimDragonbornCreate, aQuestThatIsAlreadyRunningCreatesNothingAgain)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        const std::size_t first = rig.mHooks.mPlaces.size();
        EXPECT_FALSE(rig.mHost->startQuestForTest(kQuest)) << "already running, no stage asked";
        EXPECT_EQ(rig.mHooks.mPlaces.size(), first);
    }

    TEST(SkyrimDragonbornCreate, aWorldThatCannotPlaceLeavesTheAliasesEmptyWithoutAnError)
    {
        Rig rig;
        rig.mHooks.mCanPlace = false;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        EXPECT_EQ(rig.mState.aliasFill(kQuest, 2).mReference, kNullFormId);
        EXPECT_TRUE(rig.mHost->createdObjects().empty());
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimDragonbornCreate, withoutWorldHooksNothingIsCreated)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        EXPECT_TRUE(rig.mHooks.mPlaces.empty());
        EXPECT_TRUE(rig.mHost->createdObjects().empty());
    }

    // ---- the package brain learns of the quest and of what it created -------------------------------------------

    TEST(SkyrimDragonbornReport, aQuestThatStartsIsToldAndWhatItCreatedIsReadThroughTheAliasLookup)
    {
        Rig rig;
        std::vector<FormId> started;
        rig.mHost->setQuestStarted([&](FormId quest) { started.push_back(quest); });
        const std::uint32_t before = rig.mHost->aliasRevision();
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        ASSERT_EQ(started.size(), 1u);
        EXPECT_EQ(started[0], kQuest);
        EXPECT_GT(rig.mHost->aliasRevision(), before) << "the package brain's alias reader sees a change";
        EXPECT_EQ(rig.mHost->aliasReference(kQuest, 2), static_cast<FormId>(kCreatedObjectBase + 1));
        EXPECT_EQ(rig.mHost->aliasReference(kQuest, 3), static_cast<FormId>(kCreatedObjectBase + 2));
    }

    TEST(SkyrimDragonbornReport, aQuestThatIsAlreadyRunningIsNotReportedAgain)
    {
        Rig rig;
        int starts = 0;
        rig.mHost->setQuestStarted([&](FormId) { ++starts; });
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        EXPECT_FALSE(rig.mHost->startQuestForTest(kQuest));
        EXPECT_EQ(starts, 1);
    }

    // ---- the note the cultist carries ----------------------------------------------------------------------------

    TEST(SkyrimDragonbornNote, aScriptOnTheCreatedNoteHearsOfTheNewContainerWhenItsItemMoves)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        const FormId note = static_cast<FormId>(kCreatedObjectBase + 2);
        ASSERT_EQ(rig.mHost->attachReference(note, kNoteBase, false, { noteOn() }), 1u);
        rig.tick();
        rig.mHost->onItemMoved(static_cast<FormId>(kCreatedObjectBase + 1), 0x14, kNoteBase, 1);
        rig.tick();
        EXPECT_EQ(rig.number(note, "changed"), 1.0);
        rig.mHost->onItemMoved(0x14, 0x15, kCultistBase, 1); // another item: not the note
        rig.tick();
        EXPECT_EQ(rig.number(note, "changed"), 1.0);
    }

    TEST(SkyrimDragonbornNote, readingTheItemSendsOnReadToTheScriptOnTheCreatedNoteOfThatBaseOnly)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        const FormId note = static_cast<FormId>(kCreatedObjectBase + 2);
        ASSERT_EQ(rig.mHost->attachReference(note, kNoteBase, false, { noteOn() }), 1u);
        rig.tick();
        rig.mHost->onItemRead(kCultistBase); // another book
        rig.tick();
        EXPECT_EQ(rig.number(note, "read"), 0.0);
        rig.mHost->onItemRead(kNoteBase);
        rig.tick();
        EXPECT_EQ(rig.number(note, "read"), 1.0);
    }

    // ---- a ForceGreet package walks the actor to the player (the cultists come to him) -----------------------------

    constexpr FormId kGreeterBase = 0x01000D01;
    constexpr FormId kGreetPackage = 0x01000E01;
    constexpr FormId kGreetTopic = 0x01000F01;
    constexpr openeden::sdk::game::WorldId kGreeter = 0x40;

    class GreetSource final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = { &mPackage };
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        bool forceGreetTopic(const PackageRecord&, FormId& out) const override
        {
            out = kGreetTopic;
            return true;
        }
        PackageRecord mPackage;
    };

    class GreetWriter final : public openeden::sdk::game::ActorRequestWriter
    {
    public:
        bool setMovement(openeden::sdk::game::ActorHandle, float, float) override { return true; }
        bool goTo(openeden::sdk::game::ActorHandle, const openeden::sdk::game::ActorGoal& goal) override
        {
            mGoals.push_back({ goal.x, goal.y, goal.z });
            return true;
        }
        bool cancelGoal(openeden::sdk::game::ActorHandle) override { return true; }
        bool actorPosition(openeden::sdk::game::ActorHandle, double& x, double& y, double& z) const override
        {
            x = mActor[0];
            y = mActor[1];
            z = mActor[2];
            return true;
        }
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = mPlayer[0];
            y = mPlayer[1];
            z = mPlayer[2];
            return true;
        }
        openeden::sdk::game::ActorClock clock() const override { return { 12.f, 0, true }; }
        std::vector<std::array<double, 3>> mGoals;
        std::array<double, 3> mActor{ 3000, 0, 66 };
        std::array<double, 3> mPlayer{ 0, 0, 66 };
    };

    struct GreetRig
    {
        explicit GreetRig(bool approach)
        {
            mSource.mPackage.mCommon.mId = kGreetPackage;
            mBrain = std::make_unique<PackageBrain>(mSource);
            mBrain->setApproachGreeters(approach);
            mBrain->setForceGreetHook([this](openeden::sdk::game::WorldId reference, FormId, FormId topic) {
                mAsks.push_back({ reference, topic });
            });
            openeden::sdk::game::ActorInfo info;
            info.handle = { kGreeter, 1 };
            info.base = kGreeterBase;
            info.editorId = "Cultist";
            info.x = 3000;
            info.y = 0;
            info.z = 66;
            mBrain->onActorAdded(info);
        }
        void run(int steps)
        {
            for (int i = 0; i < steps; ++i)
                mBrain->update(0.5f, mWriter);
        }
        GreetSource mSource;
        GreetWriter mWriter;
        std::unique_ptr<PackageBrain> mBrain;
        std::vector<std::pair<openeden::sdk::game::WorldId, FormId>> mAsks;
    };

    TEST(SkyrimDragonbornGreet, aGreeterFarFromThePlayerWalksToHimAndAsksOnlyWhenNear)
    {
        GreetRig rig(true);
        rig.run(8);
        ASSERT_FALSE(rig.mWriter.mGoals.empty()) << "it walks";
        EXPECT_NEAR(rig.mWriter.mGoals.front()[0], kFollowStandOff, 1e-6) << "to a point near the player";
        EXPECT_TRUE(rig.mAsks.empty()) << "3,000 units away: not yet";
        rig.mWriter.mActor = { 200, 0, 66 }; // it arrived
        rig.run(12);
        ASSERT_FALSE(rig.mAsks.empty());
        EXPECT_EQ(rig.mAsks.front().first, kGreeter);
        EXPECT_EQ(rig.mAsks.front().second, kGreetTopic);
    }

    TEST(SkyrimDragonbornGreet, aGreeterThatStandsNearAsksAtOnceAndDoesNotWalk)
    {
        GreetRig rig(true);
        rig.mWriter.mActor = { 150, 0, 66 };
        rig.run(4);
        EXPECT_FALSE(rig.mAsks.empty());
        EXPECT_TRUE(rig.mWriter.mGoals.empty());
    }

    TEST(SkyrimDragonbornGreet, withoutTheSettingTheGreeterAsksAtOnceAndStandsAsItAlwaysDid)
    {
        GreetRig rig(false);
        rig.mWriter.mActor = { 2000, 0, 66 }; // inside task 0305's own walk-up range
        rig.run(4);
        EXPECT_FALSE(rig.mAsks.empty());
        EXPECT_TRUE(rig.mWriter.mGoals.empty());
    }

    // ---- the names the pack looks the records up by -------------------------------------------------------------

    TEST(SkyrimDragonbornNames, theTestAidMarksTheWayOfTheVoiceAtStageOneHundredSixty)
    {
        EXPECT_EQ(dragonborn::kPrerequisite.mFile, "skyrim.esm");
        EXPECT_EQ(dragonborn::kPrerequisite.mLocal, 0x0242BAu);
        EXPECT_EQ(dragonborn::kPrerequisite.mStage, 160u);
        EXPECT_EQ(dragonborn::kTestPrerequisitesKey, "dragonborn test prerequisites");
    }
}
