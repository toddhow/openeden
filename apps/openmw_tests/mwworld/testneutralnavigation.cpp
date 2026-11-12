#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <apps/openmw/mwworld/neutralnavigation.hpp>

// Task 0132: the pure decisions of what a neutral world tells the navigator.
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
