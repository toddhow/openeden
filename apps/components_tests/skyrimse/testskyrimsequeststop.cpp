// Task 0344: a script that stops a quest (Quest.Stop, the last line of C00GiantAttack's stage 200) tells the package brain's
// copy of the quests, so the packages the quest's aliases gave stop. Generated records, no game data.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>

#include <gtest/gtest.h>

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

    constexpr FormId kQuest = 0x01000800;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    class NoScripts final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string&, std::vector<std::uint8_t>&) override { return false; }
        std::string formClass(FormId form) const override { return form == kQuest ? "Quest" : std::string(); }
    };

    struct Rig
    {
        Rig()
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Giant")));
            quest.append(sub4("DNAM", Bytes().u16(0).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
        }
        NoScripts mData;
        QuestIndex mIndex;
        QuestState mState;
        std::unique_ptr<PapyrusHost> mHost;
    };

    TEST(SkyrimQuestStop, aQuestAScriptStopsIsToldToThePackageBrainAndStopsRunning)
    {
        Rig rig;
        std::vector<FormId> stopped;
        rig.mHost->setQuestStopped([&](FormId quest) { stopped.push_back(quest); });
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        ASSERT_TRUE(rig.mState.running(kQuest));
        rig.mHost->stopQuestForTest(kQuest);
        EXPECT_FALSE(rig.mState.running(kQuest));
        ASSERT_EQ(stopped.size(), 1u);
        EXPECT_EQ(stopped[0], kQuest);
    }

    TEST(SkyrimQuestStop, withNoListenerAStopStillStopsTheQuest)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuest));
        rig.mHost->stopQuestForTest(kQuest);
        EXPECT_FALSE(rig.mState.running(kQuest));
    }
}
