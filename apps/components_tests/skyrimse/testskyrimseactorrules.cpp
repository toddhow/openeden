// Task 0135 stage S3: the Skyrim SE pack's side of the actor hooks (SkyrimActorTable and the pace rule). Written from the
// header first. No game data: the writer is a recording fake.
#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    struct Written
    {
        ActorHandle mHandle;
        float mVelX;
        float mVelY;
    };

    class FakeWriter final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle handle, float velX, float velY) override
        {
            mWritten.push_back({ handle, velX, velY });
            return !mRefuse;
        }
        std::vector<Written> mWritten;
        bool mRefuse = false;
    };

    ActorInfo infoOf(WorldId reference, std::uint32_t session, std::string_view editor, WorldId base = 1)
    {
        ActorInfo info;
        info.handle = { reference, session };
        info.base = base;
        info.editorId = editor;
        return info;
    }

    TEST(SkyrimActorTable, withNoTestWalkItTracksActorsAndWritesNothing)
    {
        SkyrimActorTable table;
        FakeWriter writer;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.onActorAdded(infoOf(11, 2, "Alice"));
        EXPECT_EQ(table.size(), 2u);
        EXPECT_TRUE(table.has(10));
        EXPECT_EQ(table.pacers(), 0u);
        for (int i = 0; i < 100; ++i)
            table.update(0.1f, writer);
        EXPECT_TRUE(writer.mWritten.empty());
    }

    TEST(SkyrimActorTable, onlyTheNamedActorPacesAndTheMatchIgnoresCase)
    {
        SkyrimActorTable table;
        ASSERT_TRUE(table.setTestWalk("BoB"));
        FakeWriter writer;
        table.onActorAdded(infoOf(10, 1, "bOb"));
        table.onActorAdded(infoOf(11, 2, "Alice"));
        EXPECT_EQ(table.pacers(), 1u);
        table.update(0.1f, writer);
        ASSERT_EQ(writer.mWritten.size(), 1u);
        EXPECT_EQ(writer.mWritten[0].mHandle, (ActorHandle{ 10, 1 }));
        EXPECT_EQ(writer.mWritten[0].mVelX, kPaceSpeed);
        EXPECT_EQ(writer.mWritten[0].mVelY, 0.f);
    }

    TEST(SkyrimActorTable, theDirectionFlipsEveryHalfPeriodAndOnlyChangesAreWritten)
    {
        SkyrimActorTable table;
        table.setTestWalk("bob");
        FakeWriter writer;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        const float dt = 0.5f;
        const int framesPerHalf = static_cast<int>(kPaceHalfPeriodSeconds / dt);
        for (int i = 0; i < framesPerHalf - 1; ++i)
            table.update(dt, writer);
        EXPECT_EQ(writer.mWritten.size(), 1u); // east, once
        table.update(dt, writer); // elapsed reaches the half period: west
        ASSERT_EQ(writer.mWritten.size(), 2u);
        EXPECT_EQ(writer.mWritten[1].mVelX, -kPaceSpeed);
        for (int i = 0; i < framesPerHalf; ++i)
            table.update(dt, writer);
        ASSERT_EQ(writer.mWritten.size(), 3u);
        EXPECT_EQ(writer.mWritten[2].mVelX, kPaceSpeed);
    }

    TEST(SkyrimActorTable, theSameFramesGiveTheSameRequests)
    {
        auto run = [] {
            SkyrimActorTable table;
            table.setTestWalk("bob");
            FakeWriter writer;
            table.onActorAdded(infoOf(10, 1, "Bob"));
            table.onActorAdded(infoOf(12, 3, "Bob"));
            for (int i = 0; i < 700; ++i)
                table.update(1.f / 60.f, writer);
            return writer.mWritten;
        };
        const auto a = run();
        const auto b = run();
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            EXPECT_EQ(a[i].mHandle, b[i].mHandle);
            EXPECT_EQ(a[i].mVelX, b[i].mVelX);
        }
        EXPECT_GE(a.size(), 6u); // two actors, at least three changes each in 11 s
    }

    TEST(SkyrimActorTable, aRemovedActorIsNotWrittenAgain)
    {
        SkyrimActorTable table;
        table.setTestWalk("bob");
        FakeWriter writer;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.update(0.1f, writer);
        table.onActorRemoved({ 10, 1 });
        EXPECT_EQ(table.size(), 0u);
        EXPECT_EQ(table.pacers(), 0u);
        writer.mWritten.clear();
        table.update(10.f, writer);
        EXPECT_TRUE(writer.mWritten.empty());
    }

    TEST(SkyrimActorTable, aStaleRemoveDoesNotDropTheNewerSession)
    {
        SkyrimActorTable table;
        table.setTestWalk("bob");
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.onActorAdded(infoOf(10, 5, "Bob")); // the cell reloaded before the old actor was removed
        EXPECT_EQ(table.size(), 1u);
        EXPECT_EQ(table.pacers(), 1u);
        table.onActorRemoved({ 10, 1 }); // the old session: ignored
        EXPECT_TRUE(table.has(10));
        EXPECT_EQ(table.pacers(), 1u);
        FakeWriter writer;
        table.update(0.1f, writer);
        ASSERT_EQ(writer.mWritten.size(), 1u);
        EXPECT_EQ(writer.mWritten[0].mHandle.session, 5u);
    }

    TEST(SkyrimActorTable, aRefusedRequestDropsTheRowWithoutCrashing)
    {
        SkyrimActorTable table;
        table.setTestWalk("bob");
        FakeWriter writer;
        writer.mRefuse = true;
        table.onActorAdded(infoOf(10, 1, "Bob"));
        table.update(0.1f, writer);
        EXPECT_EQ(table.size(), 0u);
        EXPECT_EQ(table.stats().mRequestsRefused, 1u);
        table.update(0.1f, writer); // nothing left to write
        EXPECT_EQ(writer.mWritten.size(), 1u);
    }

    TEST(SkyrimActorTable, anInvalidHandleIsIgnored)
    {
        SkyrimActorTable table;
        table.onActorAdded(infoOf(kNoWorldId, 1, "Bob"));
        table.onActorAdded(infoOf(10, 0, "Bob"));
        EXPECT_EQ(table.size(), 0u);
    }

    TEST(SkyrimActorTable, aTooLongTestWalkNameIsRefusedAndSwitchesTheRuleOff)
    {
        SkyrimActorTable table;
        EXPECT_TRUE(table.setTestWalk("bob"));
        EXPECT_FALSE(table.setTestWalk(std::string(kMaxActorTestWalkName + 1, 'a')));
        EXPECT_TRUE(table.testWalk().empty());
        EXPECT_TRUE(table.setTestWalk("")); // empty is simply off
    }

    TEST(SkyrimActorTable, theCellSourceIsFoundAsActorRulesAndForwardsToItsTable)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        WorldCellSource* asSource = &source;
        auto* rules = dynamic_cast<ActorRules*>(asSource);
        ASSERT_NE(rules, nullptr);
        source.actorTable().setTestWalk("bob");
        rules->onActorAdded(infoOf(10, 1, "Bob"));
        EXPECT_EQ(source.actorTable().size(), 1u);
        FakeWriter writer;
        rules->update(0.1f, writer);
        EXPECT_EQ(writer.mWritten.size(), 1u);
        rules->onActorRemoved({ 10, 1 });
        EXPECT_EQ(source.actorTable().size(), 0u);
    }
}
