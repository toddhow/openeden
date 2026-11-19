// Task 0335 (the side quest MS13 "The Golden Claw"): fn_45 GetDetected(player) for an actor that stands in the player's
// interior cell. MS13LucanCamillaSceneView's phase 4 starts only when `IsSneaking(player) == 0` and `GetDetected(player)`
// holds for Lucan: before, nothing implemented ConditionWorld::detects, so the scene waited in phase 3 for ever. Generated
// records only, no game data.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

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

    survey::Ctda getDetected(std::uint32_t target, float value = 1.f)
    {
        survey::Ctda c;
        c.mFunction = 45;
        c.mParam1 = target;
        c.mCompare = 0;
        c.mComparison = value;
        return c;
    }

    struct Fixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        Fixture()
        {
            DialogueSpeaker speaker;
            speaker.mBase = 0x01000777;
            speaker.mCell = 0x00050F1E;
            mWorld.setSpeaker(speaker);
        }
        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimGoldenClawDetect, offByDefaultNobodyDetectsThePlayer)
    {
        Fixture f;
        f.mWorld.setPlayerPlace(true, true, 0x00050F1E, kNullFormId);
        EXPECT_FALSE(f.holds(getDetected(0x14)));
    }

    TEST(SkyrimGoldenClawDetect, anActorInThePlayersInteriorDetectsHim)
    {
        Fixture f;
        f.mWorld.setDetectsPlayerInCell(true);
        f.mWorld.setPlayerPlace(true, true, 0x00050F1E, kNullFormId);
        EXPECT_TRUE(f.holds(getDetected(0x14)));
    }

    TEST(SkyrimGoldenClawDetect, anActorInAnotherCellOrWithTheOutsideUnknownDoesNot)
    {
        Fixture f;
        f.mWorld.setDetectsPlayerInCell(true);
        EXPECT_FALSE(f.holds(getDetected(0x14))) << "the player's place is not told";
        f.mWorld.setPlayerPlace(true, true, 0x0001F86D, kNullFormId);
        EXPECT_FALSE(f.holds(getDetected(0x14))) << "another interior";
        f.mWorld.setPlayerPlace(true, false, kNullFormId, 0x2A);
        EXPECT_FALSE(f.holds(getDetected(0x14))) << "an exterior square has no cell id here";
    }

    TEST(SkyrimGoldenClawDetect, onlyTheTargetThePlayerIsModelled)
    {
        Fixture f;
        f.mWorld.setDetectsPlayerInCell(true);
        f.mWorld.setPlayerPlace(true, true, 0x00050F1E, kNullFormId);
        EXPECT_FALSE(f.holds(getDetected(0x01000999))) << "another target: no data, not detected";
    }

    TEST(SkyrimGoldenClawDetect, theComparisonIsHonoured)
    {
        Fixture f;
        f.mWorld.setDetectsPlayerInCell(true);
        f.mWorld.setPlayerPlace(true, true, 0x00050F1E, kNullFormId);
        EXPECT_TRUE(f.holds(getDetected(0x14, 1.f)));
        EXPECT_FALSE(f.holds(getDetected(0x14, 0.f)));
    }

    // ---- the claw is created IN Arvel's inventory by the quest alias, also for a quest that starts with the game --------------

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kArvel = 0x01000A01; // the placed reference the alias "Arvel" is
    constexpr FormId kClaw = 0x01000B04; // the item the alias "Claw" creates
    constexpr std::uint32_t kIn = 0x80000000u; // ALCA bit 31

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes aliasRecord(std::uint32_t id, const std::string& name, const Bytes& fill)
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

    // alias 1 = Arvel (a specific reference), 2 = the claw created in alias `at`
    Bytes questBytes(std::uint16_t flags, std::uint32_t at)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes quest;
        quest.append(sub4("EDID", Bytes().zstring("MS13")));
        quest.append(sub4("DNAM", Bytes().u16(flags).u8(30).u8(0).u32(0).u32(6)));
        quest.append(sub4("NEXT", Bytes()));
        quest.append(sub4("ANAM", Bytes().u32(3)));
        quest.append(aliasRecord(1, "Arvel", sub4("ALFR", Bytes().u32(kArvel))));
        Bytes made = sub4("ALCO", Bytes().u32(kClaw));
        made.append(sub4("ALCA", Bytes().u32(at)));
        made.append(sub4("ALCL", Bytes().u32(0)));
        quest.append(aliasRecord(2, "Claw", made));
        bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, quest)));
        return bytes;
    }

    class EmptyData final : public PapyrusGameData
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
        void add(const PexAsm::ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::string formClass(FormId) const override { return std::string(); }
        bool referencePosition(FormId reference, double& x, double& y, double& z) const override
        {
            x = 100.0;
            y = 200.0;
            z = 30.0;
            return reference == kArvel;
        }
        FormId baseOf(FormId) const override { return 0; }
    };

    class ItemHooks final : public PapyrusWorldHooks
    {
    public:
        bool addItem(FormId owner, FormId item, std::int32_t count) override
        {
            mAdded.push_back({ owner, item, count });
            return true;
        }
        struct Add
        {
            FormId mOwner;
            FormId mItem;
            std::int32_t mCount;
        };
        std::vector<Add> mAdded;
        bool itemsReady() override { return mReady; }
        ObjectPlaceResult placeObject(const ObjectPlaceRequest&) override
        {
            ++mPlaced;
            return {};
        }
        bool mReady = true;
        int mPlaced = 0;
    };

    struct QuestRig
    {
        QuestRig(std::uint16_t flags, std::uint32_t at, bool inInventory, bool startNow, bool ready = true)
        {
            mHooks.mReady = ready;
            PexAsm::ScriptBuilder alias("Alias");
            mData.add(alias);
            PexAsm::ScriptBuilder refAlias("ReferenceAlias");
            refAlias.parent("Alias");
            mData.add(refAlias);
            PexAsm::ScriptBuilder watch("ClawWatch");
            watch.parent("ReferenceAlias");
            watch.var("changed", "Int");
            watch.function("OnContainerChanged")
                .param("akNewContainer", "ObjectReference")
                .param("akOldContainer", "ObjectReference")
                .iadd(PexAsm::Id("changed"), PexAsm::Id("changed"), PexAsm::Int(1));
            mData.add(watch);
            mVmad.mAliases.push_back(vmad::AliasScripts{ kQuest, 2, { scriptNamed("ClawWatch") } });
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setWorldHooks(&mHooks);
            mHost->setLog([this](bool error, const std::string& line) { mLogs.push_back((error ? "E " : "I ") + line); });
            mHost->setCreatedInInventory(inInventory);
            const Bytes bytes = questBytes(flags, at);
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [this](FormId) -> const vmad::Vmad* { return &mVmad; });
            mHost->startGame();
            if (startNow)
                EXPECT_TRUE(mHost->startQuestForTest(kQuest));
        }
        static vmad::Script scriptNamed(const char* name)
        {
            vmad::Script script;
            script.mName = name;
            return script;
        }
        ~QuestRig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mLogs)
                    std::cerr << "  " << line << std::endl;
        }
        std::vector<std::string> mLogs;
        EmptyData mData;
        ItemHooks mHooks;
        vmad::Vmad mVmad;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
    };

    TEST(SkyrimGoldenClawCreate, anInAliasPutsTheItemInTheInventoryOfTheAliasItNames)
    {
        QuestRig rig(0, kIn | 1u, true, true);
        ASSERT_EQ(rig.mHooks.mAdded.size(), 1u);
        EXPECT_EQ(rig.mHooks.mAdded[0].mOwner, kArvel);
        EXPECT_EQ(rig.mHooks.mAdded[0].mItem, kClaw);
        EXPECT_EQ(rig.mHooks.mAdded[0].mCount, 1);
        EXPECT_EQ(rig.mState.aliasFill(kQuest, 2).mReference, kClaw) << "the alias holds the item as before";
    }

    TEST(SkyrimGoldenClawCreate, withoutTheSettingNothingIsAddedAsBefore)
    {
        QuestRig rig(0, kIn | 1u, false, true);
        EXPECT_TRUE(rig.mHooks.mAdded.empty());
        EXPECT_EQ(rig.mState.aliasFill(kQuest, 2).mReference, kClaw);
    }

    TEST(SkyrimGoldenClawCreate, anAliasWhoseHolderIsNotFilledGetsNothingAdded)
    {
        QuestRig rig(0, kIn | 9u, true, true);
        EXPECT_TRUE(rig.mHooks.mAdded.empty());
    }

    TEST(SkyrimGoldenClawCreate, aQuestThatStartsWithTheGameCreatesItsAliasObjectsAtStartGame)
    {
        QuestRig rig(QuestFlags::StartGameEnabled, kIn | 1u, true, false);
        ASSERT_EQ(rig.mHooks.mAdded.size(), 1u);
        EXPECT_EQ(rig.mHooks.mAdded[0].mOwner, kArvel);
        EXPECT_EQ(rig.mState.aliasFill(kQuest, 2).mReference, kClaw);
    }

    TEST(SkyrimGoldenClawCreate, aQuestThatStartsWithTheGameCreatesNothingWithoutTheSetting)
    {
        QuestRig rig(QuestFlags::StartGameEnabled, kIn | 1u, false, false);
        EXPECT_TRUE(rig.mHooks.mAdded.empty());
        EXPECT_EQ(rig.mState.aliasFill(kQuest, 2).mReference, kNullFormId);
    }

    TEST(SkyrimGoldenClawCreate, aQuestThatDoesNotStartWithTheGameCreatesNothingAtStartGame)
    {
        QuestRig rig(0, kIn | 1u, true, false);
        EXPECT_TRUE(rig.mHooks.mAdded.empty());
    }

    TEST(SkyrimGoldenClawCreate, theItemsWaitForTheItemTableAndAreMadeOnTheTickThatFindsItReady)
    {
        QuestRig rig(QuestFlags::StartGameEnabled, kIn | 1u, true, false, false);
        EXPECT_TRUE(rig.mHooks.mAdded.empty()) << "the table is not built yet";
        rig.mHost->tick(0.1f);
        EXPECT_TRUE(rig.mHooks.mAdded.empty());
        rig.mHooks.mReady = true;
        rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.mHooks.mAdded.size(), 1u);
        rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.mHooks.mAdded.size(), 1u) << "once";
    }

    TEST(SkyrimGoldenClawCreate, aQuestThatStartsWithTheGameMakesNothingThatIsPlacedInTheWorld)
    {
        QuestRig rig(QuestFlags::StartGameEnabled, 1u, true, false);
        EXPECT_EQ(rig.mHooks.mPlaced, 0) << "only items created in an inventory are made at startGame";
        EXPECT_TRUE(rig.mHooks.mAdded.empty());
    }

    TEST(SkyrimGoldenClawCreate, theAliasScriptOfTheClawHearsOfItsNewContainerWhenTheItemLeavesArvel)
    {
        QuestRig rig(0, kIn | 1u, true, true);
        rig.mHost->tick(0.1f); // the quest's scripts attach
        rig.mHost->onItemMoved(kArvel, 0x14, kClaw, 1);
        rig.mHost->tick(0.1f);
        rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.mHost->aliasScriptNumber(kQuest, 2, "ClawWatch", "changed").value_or(-1.0), 1.0);
    }

    TEST(SkyrimGoldenClawCreate, withoutTheSettingAnAliasThatHoldsTheItemFormItselfHearsNothing)
    {
        QuestRig rig(0, kIn | 1u, false, true);
        rig.mHost->tick(0.1f); // the quest's scripts attach
        rig.mHost->onItemMoved(kArvel, 0x14, kClaw, 1);
        rig.mHost->tick(0.1f);
        rig.mHost->tick(0.1f);
        EXPECT_EQ(rig.mHost->aliasScriptNumber(kQuest, 2, "ClawWatch", "changed").value_or(-1.0), 0.0);
    }
}
