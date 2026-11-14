// Task 0159: the navmesh agent of a walking neutral actor is a cylinder of the capsule's radius. Written before the
// code; the first run was against a version that kept the session's (box) shape.
#include <gtest/gtest.h>

#include <cmath>

#include <apps/openmw/mwworld/neutralnavigation.hpp>

#include <components/detournavigator/recastparams.hpp>

namespace
{
    using namespace MWWorld;
    using DetourNavigator::AgentBounds;
    using DetourNavigator::CollisionShapeType;

    // The recast numbers of the game: scale 1/34, cell size 0.2 (components/detournavigator/settings.cpp).
    int walkableRadiusVoxels(const AgentBounds& bounds)
    {
        return static_cast<int>(std::ceil(DetourNavigator::getAgentRadius(bounds) * (1.f / 34.f) / 0.2f));
    }

    const AgentBounds session{ CollisionShapeType::Aabb, vsg::vec3(18.f, 18.f, 66.5f) };

    TEST(NeutralActorAgent, aCapsuleOfRadius22IsACylinderOfRadius22)
    {
        const AgentBounds agent = neutralActorAgentBounds(session, 22.f);
        EXPECT_EQ(agent.mShapeType, CollisionShapeType::Cylinder);
        EXPECT_EQ(agent.mHalfExtents, vsg::vec3(22.f, 22.f, 66.5f));
    }

    TEST(NeutralActorAgent, itErodesFourVoxelsWhereTheBoxCornerErodedFive)
    {
        EXPECT_EQ(walkableRadiusVoxels(neutralActorAgentBounds(session, 22.f)), 4);
        EXPECT_EQ(walkableRadiusVoxels(AgentBounds{ CollisionShapeType::Aabb, vsg::vec3(22.f, 22.f, 66.5f) }), 5);
    }

    TEST(NeutralActorAgent, aNarrowActorKeepsTheSessionsSize)
    {
        const AgentBounds agent = neutralActorAgentBounds(session, 10.f);
        EXPECT_EQ(agent.mHalfExtents, session.mHalfExtents);
        EXPECT_EQ(agent.mShapeType, CollisionShapeType::Cylinder);
    }
}

// Task 0159: carried things are obstacles of an interior's navmesh, not of an exterior's.
namespace
{
    TEST(NeutralKindsInterior, itemsBlockOnlyInAnInterior)
    {
        using openeden::sdk::game::BaseKind;
        EXPECT_TRUE(MWWorld::neutralKindBlocksNavigationIn(BaseKind::Item, true));
        EXPECT_FALSE(MWWorld::neutralKindBlocksNavigationIn(BaseKind::Item, false));
        for (const BaseKind kind : { BaseKind::Flora, BaseKind::Tree, BaseKind::Light })
            EXPECT_FALSE(MWWorld::neutralKindBlocksNavigationIn(kind, true)) << static_cast<int>(kind);
        EXPECT_TRUE(MWWorld::neutralKindBlocksNavigationIn(BaseKind::Static, false));
    }
}
