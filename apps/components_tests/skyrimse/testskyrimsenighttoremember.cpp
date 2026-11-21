// Task 0345: "A Night to Remember" (DA14, Sanguine).
//  - DA14Start's InnLocation alias tests HasSameEditorLocAsRef (180) with the alias flag in its parameter: it is the
//  same
//    question as HasSameEditorLocAsRefAlias (181), so the quest's alias filler must evaluate it for a candidate
//    location (before, 180 was answered false and the start never ran).
//  - Two aliases hold placed references of ONE base (the temple's two Alto Wines): picking one up must tell only the
//  alias
//    that holds THAT reference (DA14ItemAdd counts the pickups, four of them set stage 30; a double count ended the
//    cleaning after three).
// Written first; red against the table without 180 and against a host that does not know the pickup's placement.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location_conditions.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    const conditions::ExtraFunction* find(std::uint16_t function)
    {
        for (const conditions::ExtraFunction& e : aliasLocationExtraFunctions())
            if (e.mIndex == function)
                return &e;
        return nullptr;
    }

    TEST(SkyrimNightToRemember, hasSameEditorLocAsRefIsAnsweredLikeTheAliasForm)
    {
        const conditions::ExtraFunction* alias = find(181);
        const conditions::ExtraFunction* plain = find(180);
        ASSERT_NE(alias, nullptr);
        ASSERT_NE(plain, nullptr);
        EXPECT_EQ(plain->mIndex, 180);
        EXPECT_EQ(plain->mFunction, alias->mFunction);
    }

    TEST(SkyrimNightToRemember, theTablePinsTheEditorLocationAliasAndNoNeighbour)
    {
        EXPECT_NE(find(567), nullptr);
        EXPECT_EQ(find(179), nullptr);
    }

    // ---- two aliases hold placements of one base
    // -------------------------------------------------------------------------

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kRefA = 0x01000A01; // the wine the alias "A" holds
    constexpr FormId kRefB = 0x01000A02; // the wine the alias "B" holds
    constexpr FormId kBase = 0x01000B04; // both are placements of this item

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

    Bytes questBytes()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes quest;
        quest.append(sub4("EDID", Bytes().zstring("DA14")));
        quest.append(sub4("DNAM", Bytes().u16(0).u8(30).u8(0).u32(0).u32(6)));
        quest.append(sub4("NEXT", Bytes()));
        quest.append(sub4("ANAM", Bytes().u32(3)));
        quest.append(aliasRecord(1, "A", sub4("ALFR", Bytes().u32(kRefA))));
        quest.append(aliasRecord(2, "B", sub4("ALFR", Bytes().u32(kRefB))));
        bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, quest)));
        return bytes;
    }

    class Data final : public PapyrusGameData
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
            x = y = z = 1.0;
            return reference == kRefA || reference == kRefB;
        }
        FormId baseOf(FormId reference) const override { return reference == kRefA || reference == kRefB ? kBase : 0; }
    };

    struct Rig
    {
        Rig()
        {
            PexAsm::ScriptBuilder alias("Alias");
            mData.add(alias);
            PexAsm::ScriptBuilder refAlias("ReferenceAlias");
            refAlias.parent("Alias");
            mData.add(refAlias);
            PexAsm::ScriptBuilder watch("ItemWatch");
            watch.parent("ReferenceAlias");
            watch.var("changed", "Int");
            watch.function("OnContainerChanged")
                .param("akNewContainer", "ObjectReference")
                .param("akOldContainer", "ObjectReference")
                .iadd(PexAsm::Id("changed"), PexAsm::Id("changed"), PexAsm::Int(1));
            mData.add(watch);
            vmad::Script script;
            script.mName = "ItemWatch";
            mVmad.mAliases.push_back(vmad::AliasScripts{ kQuest, 1, { script } });
            mVmad.mAliases.push_back(vmad::AliasScripts{ kQuest, 2, { script } });
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog(
                [this](bool error, const std::string& line) { mLogs.push_back((error ? "E " : "I ") + line); });
            const Bytes bytes = questBytes();
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [this](FormId) -> const vmad::Vmad* { return &mVmad; });
            mHost->startGame();
            EXPECT_TRUE(mHost->startQuestForTest(kQuest));
            mHost->tick(0.1f); // the scripts attach
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mLogs)
                    std::cerr << "  " << line << std::endl;
        }
        double changed(std::uint32_t alias)
        {
            mHost->tick(0.1f);
            mHost->tick(0.1f);
            return mHost->aliasScriptNumber(kQuest, alias, "ItemWatch", "changed").value_or(-1.0);
        }
        std::vector<std::string> mLogs;
        Data mData;
        vmad::Vmad mVmad;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
    };

    TEST(SkyrimNightToRememberItems, pickingUpOneOfTwoPlacementsOfABaseTellsOnlyTheAliasThatHoldsIt)
    {
        Rig rig;
        rig.mHost->setPickupSource(kRefA);
        rig.mHost->onItemMoved(0, 0x14, kBase, 1);
        rig.mHost->setPickupSource(0);
        EXPECT_EQ(rig.changed(1), 1.0);
        EXPECT_EQ(rig.changed(2), 0.0) << "the other wine was not touched";
    }

    TEST(SkyrimNightToRememberItems, withNoPlacementKnownEveryAliasHoldingThatBaseHearsAsBefore)
    {
        Rig rig;
        rig.mHost->onItemMoved(0, 0x14, kBase, 1);
        EXPECT_EQ(rig.changed(1), 1.0);
        EXPECT_EQ(rig.changed(2), 1.0);
    }

    TEST(SkyrimNightToRememberItems, theSourceIsForgottenWhenItIsCleared)
    {
        Rig rig;
        rig.mHost->setPickupSource(kRefB);
        rig.mHost->setPickupSource(0);
        rig.mHost->onItemMoved(0, 0x14, kBase, 1);
        EXPECT_EQ(rig.changed(1), 1.0);
        EXPECT_EQ(rig.changed(2), 1.0);
    }
}
