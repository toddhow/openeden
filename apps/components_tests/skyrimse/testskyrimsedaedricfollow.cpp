// Task 0310 (docs/tasks/0310-skyrim-daedric-barbas.md): Barbas follows the player by the game's own packages. His
// DA03BarbasFollowPlayer (Follow template) and DA03BarbasEscortToShrine (EscortPlayerWhenNear template) both name the
// PLAYER as their target; with `setFollowPlayerPackages(true)` the brain leads the actor along behind the player for
// either (the escort's destination, a shrine 20 km away, is not walked to: hand-written, named in the task doc). Fakes
// only, no game data. Written first and run red against the brain before the change.
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kPlayerRef = 0x14;
    constexpr FormId kOtherRef = 0x01001234;
    constexpr FormId kPackFollow = 0x01000900;
    constexpr FormId kPackEscort = 0x01000901;
    constexpr FormId kPackStand = 0x01000902;

    class FakeSource final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = mList;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        bool conditionsPass(const PackageRecord& p, FormId, FormId) override
        {
            return !(p.mCommon.mId == kPackFollow && mFollowFails);
        }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        PackageActivity activityOf(const PackageRecord& p) const override
        {
            if (p.mCommon.mId == kPackFollow)
                return PackageActivity::Follow;
            if (p.mCommon.mId == kPackEscort)
                return PackageActivity::Escort;
            return PackageActivity::None;
        }
        bool targetOf(const PackageRecord& p, PackageTarget& out) const override
        {
            if (p.mCommon.mId == kPackStand)
                return false;
            out = PackageTarget();
            out.mType = PackageTargetType::SpecificReference;
            out.mValue = mTarget;
            return true;
        }
        std::vector<const PackageRecord*> mList;
        FormId mTarget = kPlayerRef;
        bool mFollowFails = false;
    };

    class FakeWriter final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle, const ActorGoal& goal) override
        {
            mGoals.push_back(goal);
            return true;
        }
        bool cancelGoal(ActorHandle) override { return true; }
        bool setHeading(ActorHandle, float) override { return true; }
        ActorClock clock() const override { return ActorClock{ 12.f, 0, true }; }
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = 5000.0;
            y = 0.0;
            z = 0.0;
            return true;
        }
        bool actorPosition(ActorHandle, double& x, double& y, double& z) const override
        {
            x = 0.0;
            y = 0.0;
            z = 0.0;
            return true;
        }
        std::vector<ActorGoal> mGoals;
    };

    struct Rig
    {
        PackageRecord mFollow;
        PackageRecord mEscort;
        PackageRecord mStand;
        FakeSource mSource;
        FakeWriter mWriter;
        PackageBrain mBrain{ mSource };

        explicit Rig(bool on)
        {
            mFollow.mCommon.mId = kPackFollow;
            mFollow.mHasPkdt = true;
            mEscort.mCommon.mId = kPackEscort;
            mEscort.mHasPkdt = true;
            mStand.mCommon.mId = kPackStand;
            mStand.mHasPkdt = true;
            mBrain.setFollowPlayerPackages(on);
            ActorInfo info;
            info.handle = { 88, 1 };
            info.base = 0x01000500;
            mBrain.onActorAdded(info);
        }

        void run(int updates)
        {
            for (int i = 0; i < updates; ++i)
                mBrain.update(1.1f, mWriter);
        }
    };

    TEST(SkyrimDaedricFollow, aFollowPackageThatNamesThePlayerLeadsTheActorToHimWhenTheBrainIsToldTo)
    {
        Rig rig(true);
        rig.mSource.mList = { &rig.mFollow };
        rig.run(6);
        ASSERT_FALSE(rig.mWriter.mGoals.empty());
        EXPECT_GT(rig.mWriter.mGoals.back().x, 3000.0); // towards the player at x = 5000, short of him
        EXPECT_LT(rig.mWriter.mGoals.back().x, 5000.0);
    }

    TEST(SkyrimDaedricFollow, anEscortPackageThatNamesThePlayerDoesTheSame)
    {
        Rig rig(true);
        rig.mSource.mList = { &rig.mEscort };
        rig.run(6);
        ASSERT_FALSE(rig.mWriter.mGoals.empty());
        EXPECT_GT(rig.mWriter.mGoals.back().x, 3000.0);
    }

    TEST(SkyrimDaedricFollow, withTheSettingOffTheActorStaysWhereItIs)
    {
        Rig rig(false);
        rig.mSource.mList = { &rig.mFollow };
        rig.run(6);
        EXPECT_TRUE(rig.mWriter.mGoals.empty());
        Rig escort(false);
        escort.mSource.mList = { &escort.mEscort };
        escort.run(6);
        EXPECT_TRUE(escort.mWriter.mGoals.empty());
    }

    TEST(SkyrimDaedricFollow, aFollowPackageThatNamesAnotherReferenceIsNotTheActorsLeadToThePlayer)
    {
        Rig rig(true);
        rig.mSource.mTarget = kOtherRef;
        rig.mSource.mList = { &rig.mFollow };
        rig.run(6);
        EXPECT_TRUE(rig.mWriter.mGoals.empty());
    }

    TEST(SkyrimDaedricFollow, whenTheFollowPackageIsNoLongerFirstTheActorStopsLeadingToThePlayer)
    {
        Rig rig(true);
        rig.mSource.mList = { &rig.mFollow, &rig.mStand };
        rig.run(4);
        ASSERT_FALSE(rig.mWriter.mGoals.empty());
        rig.mSource.mFollowFails = true; // the quest moved on: the follow package's condition fails now
        rig.run(3);
        const std::size_t before = rig.mWriter.mGoals.size();
        rig.run(8);
        EXPECT_EQ(rig.mWriter.mGoals.size(), before);
    }
}
