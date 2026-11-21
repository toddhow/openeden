// Task 0319 (milestone M-S7): Fort Dawnguard and the start of "Awakening". The pack's side, no game data: the Papyrus
// native Quest.CompleteAllObjectives (DLC1VQ01MiscObjective stage 180 calls it) finishes every objective the quest
// has shown, and the new pack setting `dawnguard` is declared and off by default. Written first and run red against
// the old host (the native was declared and not registered: it answered nothing).
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
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
    using PexAsm::Bool;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kQuestB = 0x01000801;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes stage(std::uint16_t index, std::uint8_t flags)
    {
        Bytes b;
        b.append(sub4("INDX", Bytes().u16(index).u8(flags).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        return b;
    }

    Bytes questRecord(std::uint32_t id)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("Q" + std::to_string(id & 0xFFF))));
        b.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(stage(10, 0));
        b.append(sub4("ANAM", Bytes().u32(8)));
        return record4("QUST", 0u, id, b);
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
        bool hasKeyword(FormId, FormId) const override { return false; }

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

    vmad::Vmad vmadOf(const std::string& scriptName)
    {
        vmad::Script s;
        s.mName = scriptName;
        vmad::Vmad v;
        v.mScripts.push_back(std::move(s));
        return v;
    }

    // The quest shows objectives 1 and 2 when it starts; "Finish" completes them all through the native.
    struct Rig
    {
        Rig()
        {
            ScriptBuilder form("Form");
            mData.add(form);
            ScriptBuilder quest("Quest");
            quest.parent("Form");
            quest.function("SetObjectiveDisplayed")
                .native()
                .param("aiObjective", "Int")
                .param("abDisplayed", "Bool")
                .param("abForce", "Bool");
            quest.function("CompleteAllObjectives").native();
            mData.add(quest);

            ScriptBuilder s("DgScript");
            s.parent("Quest");
            s.function("OnInit")
                .local("n", "None")
                .callMethod("SetObjectiveDisplayed", Id("self"), Id("n"), { Int(1), Bool(true), Bool(false) })
                .callMethod("SetObjectiveDisplayed", Id("self"), Id("n"), { Int(2), Bool(true), Bool(false) });
            s.function("Finish").local("n", "None").callMethod("CompleteAllObjectives", Id("self"), Id("n"));
            mData.add(s);

            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kQuestB] = "Quest";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool, const std::string& line) { mLogs.push_back(line); });

            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes all;
            all.append(questRecord(kQuestA));
            all.append(questRecord(kQuestB));
            bytes.append(group4(Content::fourCC("QUST"), 0, all));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mVmads[kQuestA] = vmadOf("DgScript");
            mHost->setQuests(&mIndex, &mState, [this](FormId questId) -> const vmad::Vmad* {
                const auto it = mVmads.find(questId);
                return it != mVmads.end() ? &it->second : nullptr;
            });
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
        std::map<FormId, vmad::Vmad> mVmads;
        std::vector<std::string> mLogs;
    };

    TEST(SkyrimDawnguardFort, completeAllObjectivesFinishesTheObjectivesTheQuestHasShown)
    {
        Rig rig;
        rig.mHost->startGame();
        rig.tick(5);
        ASSERT_TRUE(rig.mHost->objectiveState(kQuestA, 1).mDisplayed);
        ASSERT_TRUE(rig.mHost->objectiveState(kQuestA, 2).mDisplayed);
        EXPECT_FALSE(rig.mHost->objectiveState(kQuestA, 1).mCompleted);
        const std::uint64_t before = rig.mHost->objectiveRevision();

        ASSERT_TRUE(rig.mHost->callFunction(kQuestA, "DgScript", "Finish"));
        rig.tick(3);
        EXPECT_TRUE(rig.mHost->objectiveState(kQuestA, 1).mCompleted);
        EXPECT_TRUE(rig.mHost->objectiveState(kQuestA, 2).mCompleted);
        EXPECT_GT(rig.mHost->objectiveRevision(), before) << "the journal must rebuild";
    }

    TEST(SkyrimDawnguardFort, anObjectiveTheQuestNeverShowedIsNotCompleted)
    {
        Rig rig;
        rig.mHost->startGame();
        rig.tick(5);
        ASSERT_TRUE(rig.mHost->callFunction(kQuestA, "DgScript", "Finish"));
        rig.tick(3);
        const PapyrusHost::ObjectiveState unseen = rig.mHost->objectiveState(kQuestA, 3);
        EXPECT_FALSE(unseen.mDisplayed);
        EXPECT_FALSE(unseen.mCompleted);
        EXPECT_FALSE(rig.mHost->objectiveState(kQuestB, 1).mCompleted) << "another quest's objectives are its own";
    }

    TEST(SkyrimDawnguardFort, theDawnguardSettingIsDeclaredAndOffByDefault)
    {
        const std::filesystem::path manifest
            = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "game_packs" / "skyrimse" / "extension.json";
        std::ifstream in(manifest, std::ios::binary);
        ASSERT_TRUE(in.good());
        std::stringstream text;
        text << in.rdbuf();
        const std::string json = text.str();
        const std::size_t at = json.find("\"key\": \"dawnguard\"");
        ASSERT_NE(at, std::string::npos) << "the setting must be declared in extension.json";
        const std::size_t end = json.find('}', at);
        const std::string entry = json.substr(at, end - at);
        EXPECT_NE(entry.find("\"type\": \"bool\""), std::string::npos);
        EXPECT_NE(entry.find("\"default\": false"), std::string::npos);
    }
}
