// Task 0135 stage S4: the Skyrim SE pack's go-to test rule (`[extension.skyrimse] actor test goto`). Written from the
// header; no game data, the writer is a recording fake.
#include <game_skyrimse/actor_table.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    struct GoalWritten
    {
        ActorHandle mHandle;
        ActorGoal mGoal;
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle handle, float, float) override
        {
            mMoves.push_back(handle);
            return true;
        }
        bool goTo(ActorHandle handle, const ActorGoal& goal) override
        {
            mGoals.push_back({ handle, goal });
            return !mRefuse;
        }
        std::vector<ActorHandle> mMoves;
        std::vector<GoalWritten> mGoals;
        bool mRefuse = false;
    };

    ActorInfo infoOf(WorldId reference, std::uint32_t session, std::string_view editor)
    {
        ActorInfo info;
        info.handle = { reference, session };
        info.base = 1;
        info.editorId = editor;
        return info;
    }

    TEST(SkyrimActorGoto, theSettingParsesAnEditorIdAndThreeNumbers)
    {
        TestGoto goal;
        ASSERT_TRUE(parseTestGoto("Brenuin,10.5,-20,300", goal));
        EXPECT_EQ(goal.mEditorId, "brenuin");
        EXPECT_EQ(goal.mX, 10.5);
        EXPECT_EQ(goal.mY, -20.0);
        EXPECT_EQ(goal.mZ, 300.0);
        ASSERT_TRUE(parseTestGoto("  Bob , 1 , 2 , 3 ", goal));
        EXPECT_EQ(goal.mEditorId, "bob");
        EXPECT_EQ(goal.mZ, 3.0);
    }

    TEST(SkyrimActorGoto, anythingElseIsRefusedAndLeavesTheResultAlone)
    {
        TestGoto goal;
        goal.mEditorId = "keep";
        for (const std::string_view bad : { "", "Bob", "Bob,1,2", "Bob,1,2,3,4", ",1,2,3", "Bob,x,2,3", "Bob,1,,3",
                 "Bob,1,2,nan", "Bob,1,2,inf", "Bob,1,2,3abc", "Bob,1e999,2,3" })
        {
            EXPECT_FALSE(parseTestGoto(bad, goal)) << bad;
            EXPECT_EQ(goal.mEditorId, "keep") << bad;
        }
        EXPECT_FALSE(parseTestGoto(std::string(kMaxActorTestWalkName + 1, 'a') + ",1,2,3", goal));
    }

    TEST(SkyrimActorGoto, offByDefaultAndAnEmptySettingSwitchesItOff)
    {
        SkyrimActorTable table;
        EXPECT_FALSE(table.hasTestGoto());
        Writer writer;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.update(0.1f, writer);
        EXPECT_TRUE(writer.mGoals.empty());
        EXPECT_TRUE(table.setTestGoto("Bob,1,2,3"));
        EXPECT_TRUE(table.hasTestGoto());
        EXPECT_TRUE(table.setTestGoto(""));
        EXPECT_FALSE(table.hasTestGoto());
        EXPECT_FALSE(table.setTestGoto("nonsense"));
        EXPECT_FALSE(table.hasTestGoto());
    }

    TEST(SkyrimActorGoto, theNamedActorIsSentOnceToThePointAtAWalkPace)
    {
        SkyrimActorTable table;
        ASSERT_TRUE(table.setTestGoto("bOb,100,-200,30"));
        Writer writer;
        table.onActorAdded(infoOf(10, 1, "BOB"));
        table.onActorAdded(infoOf(11, 2, "Alice"));
        table.update(0.1f, writer);
        table.update(0.1f, writer);
        table.update(5.f, writer);
        ASSERT_EQ(writer.mGoals.size(), 1u);
        EXPECT_EQ(writer.mGoals[0].mHandle, (ActorHandle{ 10, 1 }));
        EXPECT_EQ(writer.mGoals[0].mGoal.x, 100.0);
        EXPECT_EQ(writer.mGoals[0].mGoal.y, -200.0);
        EXPECT_EQ(writer.mGoals[0].mGoal.z, 30.0);
        EXPECT_EQ(writer.mGoals[0].mGoal.speed, ActorSpeed::Walk);
        EXPECT_EQ(table.stats().mGoalsIssued, 1u);
        EXPECT_TRUE(writer.mMoves.empty());
    }

    TEST(SkyrimActorGoto, aRefusedGoalDropsTheRowLikeTheWalkRuleDoes)
    {
        SkyrimActorTable table;
        table.setTestGoto("bob,1,2,3");
        Writer writer;
        writer.mRefuse = true;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.update(0.1f, writer);
        EXPECT_FALSE(table.has(10));
        EXPECT_EQ(table.stats().mRequestsRefused, 1u);
        EXPECT_EQ(table.stats().mGoalsIssued, 0u);
        writer.mGoals.clear();
        table.update(0.1f, writer);
        EXPECT_TRUE(writer.mGoals.empty());
    }

    TEST(SkyrimActorGoto, resultsAreKeptByHandleAndCounted)
    {
        SkyrimActorTable table;
        table.setTestGoto("bob,1,2,3");
        Writer writer;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.update(0.1f, writer);
        EXPECT_FALSE(table.goalResultOf(10).has_value());
        table.onGoalFinished({ 10, 1 }, GoalResult::Arrived);
        ASSERT_TRUE(table.goalResultOf(10).has_value());
        EXPECT_EQ(*table.goalResultOf(10), GoalResult::Arrived);
        EXPECT_EQ(table.stats().mGoalsArrived, 1u);
        table.onGoalFinished({ 10, 1 }, GoalResult::NoPath);
        table.onGoalFinished({ 10, 1 }, GoalResult::Blocked);
        EXPECT_EQ(table.stats().mGoalsNoPath, 1u);
        EXPECT_EQ(table.stats().mGoalsBlocked, 1u);
    }

    TEST(SkyrimActorGoto, aStaleOrUnknownResultIsIgnored)
    {
        SkyrimActorTable table;
        table.setTestGoto("bob,1,2,3");
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.onActorAdded(infoOf(10, 5, "Bob")); // reloaded: the old session is stale
        table.onGoalFinished({ 10, 1 }, GoalResult::Arrived);
        table.onGoalFinished({ 99, 7 }, GoalResult::Arrived);
        EXPECT_FALSE(table.goalResultOf(10).has_value());
        EXPECT_EQ(table.stats().mGoalsArrived, 0u);
        table.onGoalFinished({ 10, 5 }, GoalResult::Arrived);
        EXPECT_TRUE(table.goalResultOf(10).has_value());
    }

    TEST(SkyrimActorGoto, aReloadedActorIsSentAgainToItsGoal)
    {
        SkyrimActorTable table;
        table.setTestGoto("bob,1,2,3");
        Writer writer;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.update(0.1f, writer);
        table.onActorRemoved({ 10, 1 });
        table.onActorAdded(infoOf(10, 4, "Bob"));
        table.update(0.1f, writer);
        ASSERT_EQ(writer.mGoals.size(), 2u);
        EXPECT_EQ(writer.mGoals[1].mHandle, (ActorHandle{ 10, 4 }));
    }

    TEST(SkyrimActorGoto, theWalkAndGotoRulesCanRunTogetherWithoutMixingUpTheirCounts)
    {
        SkyrimActorTable table;
        table.setTestWalk("alice");
        table.setTestGoto("bob,1,2,3");
        Writer writer;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.onActorAdded(infoOf(11, 2, "Alice"));
        EXPECT_EQ(table.pacers(), 1u);
        table.update(0.1f, writer);
        EXPECT_EQ(writer.mGoals.size(), 1u);
        EXPECT_EQ(writer.mMoves.size(), 1u);
        table.onActorRemoved({ 10, 1 });
        table.onActorRemoved({ 11, 2 });
        EXPECT_EQ(table.size(), 0u);
        writer = Writer{};
        table.update(10.f, writer);
        EXPECT_TRUE(writer.mGoals.empty());
        EXPECT_TRUE(writer.mMoves.empty());
    }
}
