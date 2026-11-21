// Task 0336: a walking neutral actor passes through what the navigator leaves out (flora, plants, lights, outdoor
// items, the stand-in boxes). Found in game: a Riverwood guard on a 6,500 unit path stopped `blocked` 127 units short
// of a waypoint, against TreeSwordFernCluster05, a fern the navmesh does not know. Written before the change; the first
// run was against a rule that always says no and a sweeper that ignores the set.
#include <components/resource/physicsshape.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include "ragdolltestworld.hpp"

#include "apps/openmw/mwphysics/neutralactor.hpp"
#include "apps/openmw/mwphysics/neutralmover.hpp"
#include "apps/openmw/mwworld/neutralnavigation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <unordered_set>

namespace
{
    using openeden::sdk::game::BaseKind;
    using namespace MWWorld;

    TEST(NeutralWalkThroughRule, whatTheNavigatorLeavesOutInAnExteriorIsWalkedThrough)
    {
        EXPECT_TRUE(neutralActorWalksThrough(BaseKind::Flora, false, false));
        EXPECT_TRUE(neutralActorWalksThrough(BaseKind::Light, false, false));
        EXPECT_TRUE(neutralActorWalksThrough(BaseKind::Item, false, false));
    }

    TEST(NeutralWalkThroughRule, aSolidThingIsNotWalkedThrough)
    {
        EXPECT_FALSE(neutralActorWalksThrough(BaseKind::Static, false, false));
        EXPECT_FALSE(neutralActorWalksThrough(BaseKind::Furniture, false, true));
        EXPECT_FALSE(neutralActorWalksThrough(BaseKind::Door, false, false));
    }

    TEST(NeutralWalkThroughRule, anItemInAnInteriorStillBlocksAsTheNavigatorHasIt)
    {
        EXPECT_FALSE(neutralActorWalksThrough(BaseKind::Item, false, true));
    }

    TEST(NeutralWalkThroughRule, everyTreeIsWalkedThroughBecauseTheNavmeshHasNone)
    {
        EXPECT_TRUE(neutralActorWalksThrough(BaseKind::Tree, false, false)); // a fern
        EXPECT_TRUE(neutralActorWalksThrough(BaseKind::Tree, false, false)); // a pine: the same answer, on purpose
    }

    TEST(NeutralWalkThroughRule, aStandInBoxIsWalkedThroughWhateverItIs)
    {
        EXPECT_TRUE(neutralActorWalksThrough(BaseKind::Static, true, false));
        EXPECT_TRUE(neutralActorWalksThrough(BaseKind::Activator, true, false));
    }

    TEST(NeutralLongPathPulls, theFractionsGoFromFarToNearInsideTheLine)
    {
        const auto pulls = longPathPulls();
        ASSERT_FALSE(pulls.empty());
        for (std::size_t i = 0; i < pulls.size(); ++i)
        {
            EXPECT_GT(pulls[i].mFraction, 0.f);
            EXPECT_LT(pulls[i].mFraction, 1.f);
            if (i > 0)
            {
                EXPECT_LT(pulls[i].mFraction, pulls[i - 1].mFraction);
            }
        }
    }

    class NeutralWalkThrough : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            MWPhysicsTest::ensureJoltInitialized();
            mWorld = std::make_unique<MWPhysicsTest::GameLikeWorld>(false);
            mShape = MWPhysics::NeutralActor::makeShape(MWPhysics::NeutralActor::capsuleFor(1.f));
            ASSERT_TRUE(mShape != nullptr);
            // a floor, top at z = 0, and a 60 unit high "fern" in the way
            addBox(JPH::Vec3(2000, 2000, 5), JPH::RVec3(0, 0, -5));
            mFern = addBox(JPH::Vec3(20, 20, 30), JPH::RVec3(200, 0, 30));
        }

        JPH::BodyID addBox(JPH::Vec3 half, JPH::RVec3 centre)
        {
            const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half);
            const JPH::BodyCreationSettings settings(
                shape, centre, JPH::Quat::sIdentity(), JPH::EMotionType::Static, MWPhysics::Layers::WORLD);
            return mWorld->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        }

        MWPhysics::NeutralMover::JoltSweeper sweeper(const std::unordered_set<std::uint32_t>* walkThrough) const
        {
            return MWPhysics::NeutralMover::JoltSweeper(mWorld->system(), mShape->mCollisionShape, JPH::BodyID(),
                MWPhysics::Layers::WORLD, MWPhysics::NeutralMover::Limits(), nullptr, nullptr, walkThrough);
        }

        std::unique_ptr<MWPhysicsTest::GameLikeWorld> mWorld;
        vsg::ref_ptr<Resource::PhysicsShape> mShape;
        JPH::BodyID mFern;
    };
}

TEST_F(NeutralWalkThrough, aBodyInTheSetIsNotInTheWay)
{
    const std::unordered_set<std::uint32_t> through{ mFern.GetIndexAndSequenceNumber() };
    EXPECT_FALSE(sweeper(&through).sweep(vsg::vec3(0, 0, 30), vsg::vec3(400, 0, 0)).mHit);
}

TEST_F(NeutralWalkThrough, aBodyNotInTheSetStillStopsTheSweep)
{
    const std::unordered_set<std::uint32_t> other{ 12345u };
    const MWPhysics::NeutralMover::Sweep s = sweeper(&other).sweep(vsg::vec3(0, 0, 30), vsg::vec3(400, 0, 0));
    EXPECT_TRUE(s.mHit);
    EXPECT_EQ(s.mBody, mFern.GetIndexAndSequenceNumber());
}

TEST_F(NeutralWalkThrough, noSetMeansTheOldBehaviour)
{
    EXPECT_TRUE(sweeper(nullptr).sweep(vsg::vec3(0, 0, 30), vsg::vec3(400, 0, 0)).mHit);
}

TEST_F(NeutralWalkThrough, aWalkerWalksThroughAFernInTheSetAndIsStoppedByItOtherwise)
{
    using namespace MWPhysics::NeutralMover;
    const std::unordered_set<std::uint32_t> through{ mFern.GetIndexAndSequenceNumber() };
    Request request;
    request.mVelX = 82.45f;
    vsg::vec3 feet(0, 0, 0);
    const JoltSweeper blocked = sweeper(nullptr);
    for (int i = 0; i < 600; ++i)
        feet = step(feet, request, 1.f / 60.f, blocked).mFeet;
    EXPECT_LT(feet.x, 200.f); // the fern is at x 180 to 220
    const JoltSweeper free = sweeper(&through);
    vsg::vec3 other(0, 0, 0);
    for (int i = 0; i < 600; ++i)
        other = step(other, request, 1.f / 60.f, free).mFeet;
    EXPECT_GT(other.x, 600.f);
}

TEST_F(NeutralWalkThrough, aBlockedStepNamesTheWallItHit)
{
    using namespace MWPhysics::NeutralMover;
    Request request;
    request.mVelX = 82.45f;
    vsg::vec3 feet(0, 0, 0);
    Result last;
    const JoltSweeper blocked = sweeper(nullptr);
    for (int i = 0; i < 600; ++i)
    {
        last = step(feet, request, 1.f / 60.f, blocked);
        feet = last.mFeet;
    }
    EXPECT_TRUE(last.mBlocked);
    EXPECT_EQ(last.mRefusal, Result::Refusal::Wall);
    EXPECT_EQ(last.mHitBody, mFern.GetIndexAndSequenceNumber());
}
