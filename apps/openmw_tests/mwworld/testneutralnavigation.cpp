#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/RegisterTypes.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <apps/openmw/mwworld/neutralnavigation.hpp>

#include <components/resource/physicsshape.hpp>

// Task 0134: the pure decisions of what a neutral world tells the navigator.
namespace
{
    using namespace MWWorld;
    using DetourNavigator::AgentBounds;
    using DetourNavigator::CollisionShapeType;

    const AgentBounds morrowindDefault{ CollisionShapeType::Cylinder, vsg::vec3(29.28f, 28.48f, 66.5f) };
    const AgentBounds actorOwn{ CollisionShapeType::Aabb, vsg::vec3(20.f, 21.f, 70.f) };
    const vsg::vec3 neutral(18.f, 18.f, 66.5f);

    TEST(NeutralNavigationAgent, aNeutralCellUsesTheNeutralHalfExtentsWithTheDefaultShapeType)
    {
        const AgentBounds result = choosePathfindingAgentBounds(morrowindDefault, std::nullopt, neutral);
        EXPECT_EQ(result.mShapeType, CollisionShapeType::Cylinder);
        EXPECT_EQ(result.mHalfExtents, neutral);
    }

    TEST(NeutralNavigationAgent, aNeutralCellWinsOverTheActorsOwnPhysicsBounds)
    {
        const AgentBounds result = choosePathfindingAgentBounds(morrowindDefault, actorOwn, neutral);
        EXPECT_EQ(result.mHalfExtents, neutral);
        EXPECT_EQ(result.mShapeType, CollisionShapeType::Cylinder);
    }

    TEST(NeutralNavigationAgent, withoutANeutralCellTheActorsOwnBoundsAreUsedAsBefore)
    {
        const AgentBounds result = choosePathfindingAgentBounds(morrowindDefault, actorOwn, std::nullopt);
        EXPECT_EQ(result.mShapeType, CollisionShapeType::Aabb);
        EXPECT_EQ(result.mHalfExtents, actorOwn.mHalfExtents);
    }

    TEST(NeutralNavigationAgent, withNeitherTheConfiguredDefaultIsUsedAsBefore)
    {
        const AgentBounds result = choosePathfindingAgentBounds(morrowindDefault, std::nullopt, std::nullopt);
        EXPECT_EQ(result.mShapeType, morrowindDefault.mShapeType);
        EXPECT_EQ(result.mHalfExtents, morrowindDefault.mHalfExtents);
    }

    TEST(NeutralNavigationKinds, plantsCarriedThingsAndLightsDoNotBlockButSolidThingsDo)
    {
        using openeden::sdk::game::BaseKind;
        for (const BaseKind kind : { BaseKind::Flora, BaseKind::Item, BaseKind::Light })
            EXPECT_FALSE(neutralKindBlocksNavigation(kind)) << static_cast<int>(kind);
        for (const BaseKind kind : { BaseKind::Unknown, BaseKind::Static, BaseKind::MovableStatic, BaseKind::Furniture,
                 BaseKind::Door, BaseKind::Activator, BaseKind::Container, BaseKind::Tree, BaseKind::Actor,
                 BaseKind::Other })
            EXPECT_TRUE(neutralKindBlocksNavigation(kind)) << static_cast<int>(kind);
    }

    // The stand-in box of an object whose model has no collision triangles (an effect or a marker) is not a surface:
    // docs/tasks/0134 (the cause of Whiterun's navmesh islands).
    struct JoltAllocator
    {
        JoltAllocator() { JPH::RegisterDefaultAllocator(); }
    };

    TEST(NeutralNavigationShapes, theStandInBoxIsRecognisedWhereverItsCentreIs)
    {
        const JoltAllocator allocator;
        const auto centred = Resource::makeBoxShape(vsg::vec3(50.f, 40.f, 30.f), vsg::vec3(0.f, 0.f, 0.f));
        ASSERT_NE(centred, nullptr);
        EXPECT_TRUE(neutralShapeIsStandInBox(*centred->mCollisionShape));
        // a box with an offset centre is a box inside a RotatedTranslatedShape
        const auto shifted = Resource::makeBoxShape(vsg::vec3(50.f, 40.f, 30.f), vsg::vec3(10.f, 20.f, 30.f));
        ASSERT_NE(shifted, nullptr);
        EXPECT_TRUE(neutralShapeIsStandInBox(*shifted->mCollisionShape));
    }

    TEST(NeutralNavigationShapes, aModelsOwnShapeIsNotAStandInBox)
    {
        const JoltAllocator allocator;
        const JPH::Ref<JPH::SphereShape> sphere = new JPH::SphereShape(10.f);
        EXPECT_FALSE(neutralShapeIsStandInBox(*sphere));
        // a shifted sphere is still not a box
        JPH::RotatedTranslatedShapeSettings settings(JPH::Vec3(1.f, 2.f, 3.f), JPH::Quat::sIdentity(), sphere);
        const JPH::ShapeSettings::ShapeResult result = settings.Create();
        ASSERT_FALSE(result.HasError());
        EXPECT_FALSE(neutralShapeIsStandInBox(*result.Get()));
    }

    TEST(NeutralNavigationWater, aSquareWithWaterAndLandGetsItsLevelAndSize)
    {
        const auto water = neutralNavigatorWater(true, true, -1234.5f, 4096);
        ASSERT_TRUE(water.has_value());
        EXPECT_EQ(water->mCellSize, 4096);
        EXPECT_FLOAT_EQ(water->mLevel, -1234.5f);
    }

    TEST(NeutralNavigationWater, noWaterNoLandOrABadNumberGivesNothing)
    {
        EXPECT_FALSE(neutralNavigatorWater(false, true, 0.f, 4096).has_value());
        EXPECT_FALSE(neutralNavigatorWater(true, false, 0.f, 4096).has_value());
        EXPECT_FALSE(neutralNavigatorWater(true, true, 0.f, 0).has_value());
        EXPECT_FALSE(neutralNavigatorWater(true, true, 0.f, -4096).has_value());
        EXPECT_FALSE(neutralNavigatorWater(true, true, std::numeric_limits<float>::quiet_NaN(), 4096).has_value());
        EXPECT_FALSE(neutralNavigatorWater(true, true, std::numeric_limits<float>::infinity(), 4096).has_value());
    }
}
