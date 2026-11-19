// Task 0332 (Waking Nightmare): Erandur's DA16ErandurEscortToTemple is an ESCORT package (the player is its target, the
// destination a marker in the open country). Task 0310 turned an escort into "follow the player"; now the actor leads: it
// is carried through the doors the player uses, walks to the destination once that can be reached, waits when the player
// is far behind, and when it arrives the package's end fragment runs (the quest's stage 28). Fakes only. Written before
// the change; the first run was against the brain of task 0310.
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kPlayerRef = 0x14;
    constexpr FormId kMarker = 0x24301;
    constexpr FormId kPackEscort = 0x35068;
    constexpr FormId kPackFollow = 0x35069;
    constexpr WorldId kRef = 88;

    class Source final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = mList;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId reference, double& x, double& y, double& z) const override
        {
            if (reference != kMarker || !mKnown)
                return false;
            x = mMarkerX;
            y = mMarkerY;
            z = 0;
            return true;
        }
        bool targetIsElsewhere(FormId reference) const override { return reference == kMarker && mElsewhere; }
        PackageActivity activityOf(const PackageRecord& p) const override
        {
            return p.mCommon.mId == kPackEscort ? PackageActivity::Escort : PackageActivity::Follow;
        }
        bool targetOf(const PackageRecord&, PackageTarget& out) const override
        {
            out = PackageTarget();
            out.mType = PackageTargetType::SpecificReference;
            out.mValue = kPlayerRef;
            return true;
        }
        std::vector<const PackageRecord*> mList;
        bool mKnown = false;
        bool mElsewhere = true;
        double mMarkerX = 6000;
        double mMarkerY = 0;
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float vx, float vy) override
        {
            mMoves.push_back({ vx, vy });
            return true;
        }
        bool goTo(ActorHandle, const ActorGoal& goal) override
        {
            mGoals.push_back(goal);
            return true;
        }
        bool cancelGoal(ActorHandle) override
        {
            ++mCancels;
            return true;
        }
        bool setHeading(ActorHandle, float) override { return true; }
        ActorClock clock() const override { return ActorClock{ 12.f, 0, true }; }
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = mPlayerX;
            y = 0;
            z = 0;
            return true;
        }
        bool actorPosition(ActorHandle, double& x, double& y, double& z) const override
        {
            x = mActorX;
            y = 0;
            z = 0;
            return true;
        }
        std::vector<ActorGoal> mGoals;
        std::vector<std::pair<float, float>> mMoves;
        int mCancels = 0;
        double mPlayerX = 300;
        double mActorX = 0;
    };

    struct Rig
    {
        PackageRecord mEscort;
        PackageRecord mFollow;
        Source mSource;
        Writer mWriter;
        PackageBrain mBrain{ mSource };
        std::vector<WorldId> mEnded;

        explicit Rig(bool escort = true, bool on = true)
        {
            mEscort.mCommon.mId = kPackEscort;
            mEscort.mHasPkdt = true;
            mEscort.mLocations.push_back({ PackageLocationType::NearReference, kMarker, 100 });
            mEscort.mEndScript = "PF_DA16ErandurEscortToTemple_00035068";
            mEscort.mEndFunction = "Fragment_0";
            mFollow.mCommon.mId = kPackFollow;
            mFollow.mHasPkdt = true;
            mSource.mList = { escort ? &mEscort : &mFollow };
            mBrain.setFollowPlayerPackages(on);
            mBrain.setPackageEndHook([this](WorldId reference, const PackageRecord&) { mEnded.push_back(reference); });
            ActorInfo info;
            info.handle = { kRef, 1 };
            info.base = 0x24280;
            mBrain.onActorAdded(info);
        }
        void run(int updates)
        {
            for (int i = 0; i < updates; ++i)
                mBrain.update(1.1f, mWriter);
        }
    };

    TEST(SkyrimDaedricEscort, an_escort_to_a_marker_in_another_cell_follows_the_player_and_is_carried_through_doors)
    {
        Rig rig;
        rig.mWriter.mPlayerX = 3000;
        rig.run(6);
        ASSERT_FALSE(rig.mWriter.mGoals.empty());
        EXPECT_LT(rig.mWriter.mGoals.back().x, 3000.0); // towards the player, short of him
        EXPECT_EQ(rig.mBrain.travellerRefs().count(kRef), 1u);
    }

    TEST(SkyrimDaedricEscort, a_plain_follow_package_is_carried_through_doors_too)
    {
        Rig rig(false);
        rig.mWriter.mPlayerX = 3000;
        rig.run(6);
        EXPECT_EQ(rig.mBrain.travellerRefs().count(kRef), 1u);
    }

    TEST(SkyrimDaedricEscort, with_the_setting_off_nothing_is_carried)
    {
        Rig rig(true, false);
        rig.run(6);
        EXPECT_EQ(rig.mBrain.travellerRefs().count(kRef), 0u);
    }

    TEST(SkyrimDaedricEscort, once_the_marker_can_be_reached_and_the_player_is_near_the_actor_walks_to_it)
    {
        Rig rig;
        rig.run(4);
        rig.mSource.mElsewhere = false;
        rig.mSource.mKnown = true;
        rig.run(6);
        ASSERT_FALSE(rig.mWriter.mGoals.empty());
        EXPECT_NEAR(rig.mWriter.mGoals.back().x, 6000.0, 1.0);
        EXPECT_NEAR(rig.mWriter.mGoals.back().y, 0.0, 1.0);
    }

    TEST(SkyrimDaedricEscort, a_marker_far_beyond_the_loaded_country_is_not_headed_for_yet)
    {
        // the navigator knows only the cells round the player: a destination 20,000 units away has no path, so the
        // actor keeps to the player until it is within reach of it
        Rig rig;
        rig.run(4);
        rig.mSource.mElsewhere = false;
        rig.mSource.mKnown = true;
        rig.mSource.mMarkerX = 20000;
        rig.mWriter.mPlayerX = 300;
        rig.run(6);
        for (const ActorGoal& g : rig.mWriter.mGoals)
            EXPECT_LT(g.x, 1000.0);
        rig.mSource.mMarkerX = 5000; // now within reach
        rig.run(6);
        ASSERT_FALSE(rig.mWriter.mGoals.empty());
        EXPECT_NEAR(rig.mWriter.mGoals.back().x, 5000.0, 1.0);
    }

    TEST(SkyrimDaedricEscort, where_the_navigator_has_no_path_the_escort_walks_straight_at_the_marker)
    {
        // the exterior navmesh of a creature-sized agent takes minutes to build (task 0134, task 0280): the escort
        // does not stand for good after "no path", it walks the leg directly
        Rig rig;
        rig.run(4);
        rig.mSource.mElsewhere = false;
        rig.mSource.mKnown = true;
        rig.run(6);
        const std::size_t goals = rig.mWriter.mGoals.size();
        ASSERT_GT(goals, 0u);
        rig.mBrain.onGoalFinished({ kRef, 1 }, GoalResult::NoPath);
        rig.mWriter.mMoves.clear();
        rig.run(6);
        ASSERT_FALSE(rig.mWriter.mMoves.empty());
        EXPECT_GT(rig.mWriter.mMoves.back().first, 50.f); // towards +x, at about a walking pace
        EXPECT_LT(rig.mWriter.mMoves.back().first, 120.f);
        EXPECT_NEAR(rig.mWriter.mMoves.back().second, 0.f, 1.f);
        EXPECT_EQ(rig.mWriter.mGoals.size(), goals); // no new goal for the navigator
    }

    TEST(SkyrimDaedricEscort, the_direct_walk_stops_on_arrival)
    {
        Rig rig;
        rig.run(4);
        rig.mSource.mElsewhere = false;
        rig.mSource.mKnown = true;
        rig.run(6);
        rig.mBrain.onGoalFinished({ kRef, 1 }, GoalResult::NoPath);
        rig.run(3);
        rig.mWriter.mActorX = 5950;
        rig.mWriter.mPlayerX = 5800;
        rig.mWriter.mMoves.clear();
        rig.run(4);
        ASSERT_EQ(rig.mEnded.size(), 1u);
        ASSERT_FALSE(rig.mWriter.mMoves.empty());
        EXPECT_EQ(rig.mWriter.mMoves.back().first, 0.f); // stopped
    }

    TEST(SkyrimDaedricEscort, when_the_player_is_far_behind_the_actor_waits_for_him)
    {
        Rig rig;
        rig.run(4);
        rig.mSource.mElsewhere = false;
        rig.mSource.mKnown = true;
        rig.mWriter.mPlayerX = -4000; // far behind
        rig.run(2);
        for (const ActorGoal& g : rig.mWriter.mGoals)
            EXPECT_LT(g.x, 5000.0); // never a goal at the marker while he is far
        const std::size_t before = rig.mWriter.mGoals.size();
        rig.run(8);
        for (std::size_t i = before; i < rig.mWriter.mGoals.size(); ++i)
            EXPECT_LT(rig.mWriter.mGoals[i].x, 5000.0);
        EXPECT_TRUE(rig.mEnded.empty());
    }

    TEST(SkyrimDaedricEscort, arriving_at_the_marker_runs_the_end_fragment_once_and_ends_the_following)
    {
        Rig rig;
        rig.run(4);
        rig.mSource.mElsewhere = false;
        rig.mSource.mKnown = true;
        rig.run(3);
        rig.mWriter.mActorX = 5950; // at the marker, within its radius
        rig.mWriter.mPlayerX = 5800;
        rig.run(6);
        ASSERT_EQ(rig.mEnded.size(), 1u);
        EXPECT_EQ(rig.mEnded.front(), kRef);
        EXPECT_EQ(rig.mBrain.travellerRefs().count(kRef), 0u);
        const std::size_t goals = rig.mWriter.mGoals.size();
        rig.mWriter.mPlayerX = 1000; // he walks away: the actor no longer follows
        rig.run(8);
        EXPECT_EQ(rig.mWriter.mGoals.size(), goals);
        EXPECT_EQ(rig.mEnded.size(), 1u);
    }
}
