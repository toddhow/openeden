// Task 0330 (Dawnguard, Dimhollow Crypt): the book of the quest "Awakening" is a quest ALIAS holding a placed reference and
// carries a script (DLC1VQ01BookScript) whose OnRead sets stage 17. A placed book read in the world sent OnRead only to the
// scripts of the reference itself, never to the scripts of an alias that holds it. Generated records, a fake game and fake
// hooks; no game data. Written first, run red against the old host.
#include <game_skyrimse/deaths.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dragonborn.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/records.hpp>
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
    constexpr FormId kAxeList = 0x01000B03; // a leveled item
    constexpr FormId kAxe = 0x01000B04; // what it resolves to
    constexpr std::uint32_t kIn = 0x80000000u; // ALCA bit 31
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
        quest.append(alias(2, "Axe", created(kAxeList, kIn | 1u, 3)));
        quest.append(alias(3, "Cultist", created(kCultistBase, 1, 0)));
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
        FormId resolveLeveledItem(FormId base) override { return mResolve ? mResolved : base; }
        std::vector<ObjectPlaceRequest> mPlaces;
        bool mCanPlace = true;
        bool mResolve = true;
        FormId mResolved = kAxe;
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
            mVmad.mAliases.push_back({ kQuest, 1, { aliasScript() } });
            mHost->setQuests(&mIndex, &mState, [this](FormId q) -> const vmad::Vmad* { return q == kQuest ? &mVmad : nullptr; });
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
        std::optional<double> aliasNumber(const char* name)
        {
            return mHost->aliasScriptNumber(kQuest, 1, "NoteScript", name);
        }
        static vmad::Script aliasScript()
        {
            vmad::Script s;
            s.mName = "NoteScript";
            return s;
        }
        vmad::Vmad mVmad;
        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        QuestRecords mRecords;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimDawnguardAwake, aScriptOnAnAliasThatHoldsTheReferenceHearsItIsRead)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        rig.tick();
        rig.mHost->onRead(kMarker);
        rig.tick();
        ASSERT_TRUE(rig.aliasNumber("read").has_value());
        EXPECT_EQ(rig.aliasNumber("read").value(), 1.0);
    }

    TEST(SkyrimDawnguardAwake, anotherReferenceBeingReadTellsTheAliasScriptNothing)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        rig.tick();
        rig.mHost->onRead(0x01000A99);
        rig.tick();
        ASSERT_TRUE(rig.aliasNumber("read").has_value());
        EXPECT_EQ(rig.aliasNumber("read").value(), 0.0);
    }

    // ---- a scene phase waits for HasLoaded3D of its actors' aliases (the puzzle room scene: Adalvald and the boss)

    TEST(SkyrimDawnguardAwake, hasLoaded3DIsTheLookupsAnswerForAnyoneButThePlayerWhoAlwaysHasIt)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        const conditions::Subject vampire = 0x0200FBF5;
        EXPECT_FALSE(world.hasLoaded3D(vampire)) << "nothing says the actor is loaded";
        EXPECT_TRUE(world.hasLoaded3D(world.playerSubject()));
        world.setLoadedLookup([](FormId reference) { return reference == 0x0200FBF5; });
        EXPECT_TRUE(world.hasLoaded3D(vampire));
        EXPECT_FALSE(world.hasLoaded3D(0x0200FBF6));
        EXPECT_TRUE(world.hasLoaded3D(SkyrimConditionWorld::kPlacedTag | 0x0200FBF5)) << "the subject an alias resolves to";
        EXPECT_FALSE(world.hasLoaded3D(SkyrimConditionWorld::kBaseTag | 0x0200FBF5)) << "a base has no 3D";
        EXPECT_TRUE(world.hasLoaded3D(world.playerSubject()));
    }

    // ---- the puzzle room is dry: a plane at 0.0 that lies 900 units over a quarter of the content is not believed

    TEST(SkyrimDawnguardAwake, aPlaneThatLiesFarOverAQuarterOfTheContentIsNotBelieved)
    {
        InteriorWaterEvidence evidence;
        evidence.mAnyObject = true;
        evidence.mMinObjectZ = -1500.f;
        evidence.mAnyDoor = true;
        evidence.mMinDoorZ = -4.f; // the entrance stands at the plane, like Dimhollow Crypt's second cell
        evidence.mCount = 100;
        evidence.mLowQuarterZ = -900.f; // a quarter of the content is 900 under the plane
        EXPECT_FALSE(interiorWaterPlausible(0.f, evidence));
    }

    TEST(SkyrimDawnguardAwake, aPlaneThatLiesWithinReachOfAQuarterOfTheContentIsStillBelieved)
    {
        InteriorWaterEvidence evidence;
        evidence.mAnyObject = true;
        evidence.mMinObjectZ = -300.f;
        evidence.mAnyDoor = true;
        evidence.mMinDoorZ = -125.f; // the Ratway
        evidence.mCount = 100;
        evidence.mLowQuarterZ = -150.f; // a quarter of the content is 150 under the plane
        EXPECT_TRUE(interiorWaterPlausible(0.f, evidence));
    }

    TEST(SkyrimDawnguardAwake, evidenceThatCountsNothingIsJudgedAsBefore)
    {
        InteriorWaterEvidence evidence;
        evidence.mAnyObject = true;
        evidence.mMinObjectZ = -1500.f;
        EXPECT_TRUE(interiorWaterPlausible(0.f, evidence));
    }

    // ---- the corpses placed dead (TreasCorpse...: the puzzle room's decor) stay dead

    TEST(SkyrimDawnguardAwake, anActorThatStartsDeadGetsALedgerEntryAtTheClocksHourAndOnlyOne)
    {
        DeathLedger ledger;
        DeathKeeper keeper(ledger, DeathHooks());
        keeper.setClock(100.0, true);
        EXPECT_TRUE(keeper.noteStartsDead(0x02012345));
        ASSERT_NE(ledger.find(0x02012345), nullptr);
        EXPECT_EQ(ledger.find(0x02012345)->mHours, 100.0);
        EXPECT_FALSE(ledger.find(0x02012345)->mHasPosition) << "it lies where it was placed";
        EXPECT_FALSE(ledger.find(0x02012345)->mPlaced) << "the keeper still has to put it back";
        keeper.setClock(130.0, true);
        EXPECT_FALSE(keeper.noteStartsDead(0x02012345)) << "an entry exists already: its hour stays";
        EXPECT_EQ(ledger.find(0x02012345)->mHours, 100.0);
    }

    TEST(SkyrimDawnguardAwake, aNullOrCreatedReferenceIsNotNotedAsDead)
    {
        DeathLedger ledger;
        DeathKeeper keeper(ledger, DeathHooks());
        EXPECT_FALSE(keeper.noteStartsDead(0));
        EXPECT_FALSE(keeper.noteStartsDead(static_cast<FormId>(openeden::sdk::game::kCreatedObjectBase) + 5));
        EXPECT_EQ(ledger.size(), 0u);
    }
}
