// Task 0269 (milestone M-S7): the other four purchasable houses (Honeyside, Proudspire Manor, Vlindrel Hall, Hjerim).
// The pack's side: the developer aid's list of jarls whose goodwill the offers need, and the host aid that marks a
// quest stage done WITHOUT running its fragments (Hjerim's offer needs MS11 finished). Written FIRST, against stubs.
#include <game_skyrimse/houses.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
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
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800;

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
        bool referencePosition(FormId, double&, double&, double&) const override { return false; }
        FormId baseOf(FormId) const override { return 0; }
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

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder cell("Cell");
        cell.parent("Form");
        cell.function("SetFactionOwner").native().param("akFaction", "Form");
        cell.function("SetActorOwner").native().param("akActor", "Form");
        data.add(cell);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        data.add(actor);
        ScriptBuilder quest("Quest");
        quest.parent("Form");
        quest.function("SetStage", "Bool").native().param("auiStage", "Int");
        data.add(quest);
        ScriptBuilder game("Game");
        game.function("IncrementStat").global().native().param("asStatName", "String").param("aiModAmount", "Int");
        data.add(game);
    }

    ScriptBuilder fragmentScript()
    {
        ScriptBuilder s("QF_Houses");
        s.parent("Quest");
        s.var("ranZero", "Int");
        s.var("ranOne", "Int");
        s.var("HouseVar", "Int", Int(7));
        s.function("Fragment_0").assign(Id("ranZero"), Int(1));
        s.function("Fragment_1").assign(Id("ranOne"), Int(1));
        return s;
    }

    Bytes stageBytes(std::uint16_t index)
    {
        Bytes b;
        b.append(sub4("INDX", Bytes().u16(index).u8(0).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        return b;
    }

    struct Rig
    {
        Rig()
        {
            addClasses(mData);
            mData.add(fragmentScript());
            mData.mClasses[kQuestA] = "Quest";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            quest.append(stageBytes(10));
            quest.append(sub4("ANAM", Bytes().u32(8)));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mVmad.mHasFragments = true;
            mVmad.mFragmentScript = "QF_Houses";
            vmad::Script qs;
            qs.mName = "QF_Houses";
            mVmad.mScripts = { qs };
            mVmad.mFragments.push_back({ 10, 0, "QF_Houses", "Fragment_0" });
            mVmad.mFragments.push_back({ 10, 1, "QF_Houses", "Fragment_1" });
            mHost->setQuests(&mIndex, &mState,
                [this](FormId questId) -> const vmad::Vmad* { return questId == kQuestA ? &mVmad : nullptr; });
            mHost->startGame();
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

        FakeData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        vmad::Vmad mVmad;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimHouses2Data, theTestAidBefriendsTheJarlsOfAllFiveHolds)
    {
        const std::vector<FormId> bases = houses::testPermissionJarlBases();
        const std::set<FormId> unique(bases.begin(), bases.end());
        EXPECT_EQ(unique.size(), bases.size()) << "no jarl twice";
        // Whiterun: Balgruuf and Vignar; Windhelm: Ulfric and Brunwulf; Riften: Laila and Maven; Markarth: Igmund and
        // Thongvor; Solitude: Elisif (the offers' aliases 10, 14, 11, 15, 12, 16, 13, 17 and 9)
        for (const FormId expected :
            { 0x13BBDu, 0x13BB5u, 0x1414Du, 0x14149u, 0x13366u, 0x1336Au, 0x1339Fu, 0x133BBu, 0x1326Au })
            EXPECT_TRUE(unique.count(expected) == 1) << std::hex << expected;
        EXPECT_EQ(bases.size(), 9u);
    }

    TEST(SkyrimHouses2Data, theMs11QuestIsHjerimsPermission)
    {
        EXPECT_EQ(houses::kMs11Quest, 0x1F7A3u);
    }

    TEST(SkyrimHouses2Host, aStageMarkedByTheAidIsDoneAndCurrentAndRunsNoFragment)
    {
        Rig rig;
        EXPECT_FALSE(rig.mHost->markStageDoneForTest(0x01000999, 10)) << "an unknown quest";
        ASSERT_TRUE(rig.mHost->markStageDoneForTest(kQuestA, 10));
        rig.tick();
        EXPECT_TRUE(rig.mState.stageDone(kQuestA, 10));
        EXPECT_EQ(rig.mState.stage(kQuestA), 10.0f);
        EXPECT_EQ(rig.mHost->numberVariable(kQuestA, "QF_Houses", "ranZero"), 0.0);
        EXPECT_EQ(rig.mHost->numberVariable(kQuestA, "QF_Houses", "ranOne"), 0.0);
    }

    TEST(SkyrimHouses2Host, aStageThatIsAlreadyDoneIsMarkedAgainWithoutTrouble)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->markStageDoneForTest(kQuestA, 10));
        EXPECT_TRUE(rig.mHost->markStageDoneForTest(kQuestA, 10));
        EXPECT_TRUE(rig.mErrors.empty());
    }

    // ---- the steward's faction from the dialogue quest's alias ------------------------------------------------------

    constexpr FormId kQuestSteward = 0x01000900;
    constexpr FormId kRefFalk = 0x01000901;
    constexpr FormId kRefOther = 0x01000902;
    constexpr FormId kFactionSteward = 0x01000903;
    constexpr FormId kFactionOtherJob = 0x01000904;

    Bytes aliasBytes(std::uint32_t id, const std::string& name, FormId reference, const std::vector<FormId>& factions)
    {
        Bytes b;
        b.append(sub4("ALST", Bytes().u32(id)));
        b.append(sub4("ALID", Bytes().zstring(name)));
        b.append(sub4("FNAM", Bytes().u32(0)));
        b.append(sub4("ALFR", Bytes().u32(reference)));
        for (const FormId f : factions)
            b.append(sub4("ALFC", Bytes().u32(f)));
        b.append(sub4("ALED", Bytes()));
        return b;
    }

    struct QuestIndexRig
    {
        QuestIndexRig()
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("DialogueSolitudeTest")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            quest.append(sub4("ANAM", Bytes().u32(8)));
            quest.append(aliasBytes(22, "Steward", kRefFalk, { kFactionSteward }));
            quest.append(aliasBytes(23, "Blacksmith", kRefOther, { kFactionOtherJob }));
            quest.append(aliasBytes(24, "Nobody", kRefOther, {}));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestSteward, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
        }
        QuestIndex mIndex;
    };

    TEST(SkyrimHouses2Steward, anAliasThatAddsTheStewardFactionGrantsItToItsDataFilledReference)
    {
        QuestIndexRig rig;
        const std::vector<houses::AliasFactionGrant> grants
            = houses::aliasFactionGrants(rig.mIndex, { kQuestSteward }, kFactionSteward);
        ASSERT_EQ(grants.size(), 1u);
        EXPECT_EQ(grants[0].mReference, kRefFalk);
        EXPECT_EQ(grants[0].mFaction, kFactionSteward);
    }

    TEST(SkyrimHouses2Steward, otherFactionsAliasesWithNoFactionsAndUnknownQuestsGrantNothing)
    {
        QuestIndexRig rig;
        EXPECT_TRUE(houses::aliasFactionGrants(rig.mIndex, { kQuestSteward }, 0x01000999).empty());
        EXPECT_TRUE(houses::aliasFactionGrants(rig.mIndex, { 0x01000999 }, kFactionSteward).empty());
        EXPECT_TRUE(houses::aliasFactionGrants(rig.mIndex, {}, kFactionSteward).empty());
    }

    TEST(SkyrimHouses2Steward, theHoldDialogueQuestsAreTheFiveOfTheGame)
    {
        EXPECT_EQ(
            houses::holdDialogueQuests(), (std::vector<FormId>{ 0x270A4u, 0x368D8u, 0x3502Eu, 0x17F05u, 0x2E1ADu }));
    }
}
