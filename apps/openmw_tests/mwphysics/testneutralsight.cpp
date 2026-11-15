// Task 0171 (combat stage S5-3b): the line-of-sight ray of a melee blow against a real Jolt system. A wall and a closed
// door block; a neutral actor's capsule body (also a WORLD body) listed as ignored does not; a wall behind the target
// or against it does not. Written from the header before the code (the first run was against a stub).
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include "ragdolltestworld.hpp"

#include "apps/openmw/mwphysics/neutralsight.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <memory>

namespace
{
    using MWPhysics::NeutralSight::blocked;

    class NeutralSightJolt : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            MWPhysicsTest::ensureJoltInitialized();
            mWorld = std::make_unique<MWPhysicsTest::GameLikeWorld>(false);
        }

        JPH::BodyID addBox(JPH::ObjectLayer layer, JPH::Vec3 half, JPH::RVec3 centre)
        {
            const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half);
            const JPH::BodyCreationSettings settings(shape, centre, JPH::Quat::sIdentity(), JPH::EMotionType::Static, layer);
            return mWorld->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        }

        // A wall across y = 30, 200 wide and 200 high, 4 thick.
        JPH::BodyID addWall(JPH::ObjectLayer layer = MWPhysics::Layers::WORLD)
        {
            return addBox(layer, JPH::Vec3(100.f, 2.f, 100.f), JPH::RVec3(0.0, 30.0, 100.0));
        }

        static constexpr int kStatic = MWPhysics::Layers::WORLD | MWPhysics::Layers::DOOR | MWPhysics::Layers::HEIGHTMAP;
        const vsg::vec3 mFrom{ 0.f, 0.f, 80.f };
        const vsg::vec3 mTo{ 0.f, 60.f, 80.f };

        std::unique_ptr<MWPhysicsTest::GameLikeWorld> mWorld;
    };

    TEST_F(NeutralSightJolt, anEmptyWorldBlocksNothing)
    {
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, mTo, kStatic, {}));
    }

    TEST_F(NeutralSightJolt, aWallBetweenTheTwoPointsBlocks)
    {
        addWall();
        EXPECT_TRUE(blocked(mWorld->system(), mFrom, mTo, kStatic, {}));
        // From either side.
        EXPECT_TRUE(blocked(mWorld->system(), mTo, mFrom, kStatic, {}));
    }

    TEST_F(NeutralSightJolt, aClosedDoorBlocksWhenTheDoorLayerIsInTheMask)
    {
        addWall(MWPhysics::Layers::DOOR);
        EXPECT_TRUE(blocked(mWorld->system(), mFrom, mTo, kStatic, {}));
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, mTo, MWPhysics::Layers::WORLD, {})); // the control: the mask matters
    }

    TEST_F(NeutralSightJolt, anActorsCapsuleBodyInTheWayDoesNotBlockWhenItIsIgnored)
    {
        // The actors' bodies are WORLD objects: here a 44 wide box standing at y = 30.
        const JPH::BodyID actor = addBox(MWPhysics::Layers::WORLD, JPH::Vec3(22.f, 22.f, 64.f), JPH::RVec3(0.0, 30.0, 64.0));
        EXPECT_TRUE(blocked(mWorld->system(), mFrom, mTo, kStatic, {})); // control: unlisted, it blocks
        const JPH::BodyID ignored[] = { actor };
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, mTo, kStatic, ignored));
    }

    TEST_F(NeutralSightJolt, anIgnoredActorDoesNotHideAWallBehindIt)
    {
        const JPH::BodyID actor = addBox(MWPhysics::Layers::WORLD, JPH::Vec3(22.f, 22.f, 64.f), JPH::RVec3(0.0, 20.0, 64.0));
        addWall();
        const JPH::BodyID ignored[] = { actor };
        EXPECT_TRUE(blocked(mWorld->system(), mFrom, mTo, kStatic, ignored));
    }

    TEST_F(NeutralSightJolt, aWallBehindTheTargetDoesNotBlock)
    {
        addBox(MWPhysics::Layers::WORLD, JPH::Vec3(100.f, 2.f, 100.f), JPH::RVec3(0.0, 90.0, 100.0));
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, mTo, kStatic, {}));
    }

    TEST_F(NeutralSightJolt, aWallTheTargetStandsAgainstDoesNotBlock)
    {
        // The wall's near face is at y = 59 (1 unit before the end of the segment): inside the trimmed end.
        addBox(MWPhysics::Layers::WORLD, JPH::Vec3(100.f, 1.5f, 100.f), JPH::RVec3(0.0, 60.5, 100.0));
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, mTo, kStatic, {}));
    }

    TEST_F(NeutralSightJolt, aWallBesideTheLineDoesNotBlock)
    {
        addBox(MWPhysics::Layers::WORLD, JPH::Vec3(2.f, 100.f, 100.f), JPH::RVec3(60.0, 30.0, 100.0));
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, mTo, kStatic, {}));
    }

    TEST_F(NeutralSightJolt, aLineBelowAWallsTopButAboveItsFootStillMeetsIt)
    {
        // A low counter (50 high): a blow aimed at 80 clears it, one aimed at 30 does not.
        addBox(MWPhysics::Layers::WORLD, JPH::Vec3(100.f, 2.f, 25.f), JPH::RVec3(0.0, 30.0, 25.0));
        EXPECT_FALSE(blocked(mWorld->system(), vsg::vec3(0.f, 0.f, 80.f), vsg::vec3(0.f, 60.f, 80.f), kStatic, {}));
        EXPECT_TRUE(blocked(mWorld->system(), vsg::vec3(0.f, 0.f, 30.f), vsg::vec3(0.f, 60.f, 30.f), kStatic, {}));
    }

    TEST_F(NeutralSightJolt, aSegmentThatIsNotFiniteOrTooShortIsNeverBlocked)
    {
        addWall();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(blocked(mWorld->system(), vsg::vec3(nan, 0.f, 80.f), mTo, kStatic, {}));
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, vsg::vec3(0.f, nan, 80.f), kStatic, {}));
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, mFrom, kStatic, {}));
        EXPECT_FALSE(blocked(mWorld->system(), mFrom, vsg::vec3(0.f, 1.f, 80.f), kStatic, {}));
    }

    TEST_F(NeutralSightJolt, aRayCastAllocatesNothing)
    {
        addWall();
        const JPH::BodyID ignored[] = { addBox(MWPhysics::Layers::WORLD, JPH::Vec3(1.f, 1.f, 1.f), JPH::RVec3(500.0, 0.0, 0.0)) };
        (void)blocked(mWorld->system(), mFrom, mTo, kStatic, ignored); // warm up
        std::size_t allocations = 0;
        bool all = true;
        {
            TestSupport::AllocationCounter counter;
            for (int i = 0; i < 200; ++i)
                all = blocked(mWorld->system(), mFrom, mTo, kStatic, ignored) && all;
            allocations = counter.count();
        }
        EXPECT_TRUE(all);
        EXPECT_EQ(allocations, 0u);
    }
}
