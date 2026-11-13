#include "neutralnavigation.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <cmath>

namespace MWWorld
{
    DetourNavigator::AgentBounds choosePathfindingAgentBounds(const DetourNavigator::AgentBounds& configuredDefault,
        const std::optional<DetourNavigator::AgentBounds>& physicsActor,
        const std::optional<vsg::vec3>& neutralHalfExtents)
    {
        if (neutralHalfExtents.has_value())
            return DetourNavigator::AgentBounds{ configuredDefault.mShapeType, *neutralHalfExtents };
        if (physicsActor.has_value())
            return *physicsActor;
        return configuredDefault;
    }

    bool neutralKindBlocksNavigation(openeden::sdk::game::BaseKind kind)
    {
        using openeden::sdk::game::BaseKind;
        return kind != BaseKind::Flora && kind != BaseKind::Tree && kind != BaseKind::Item && kind != BaseKind::Light;
    }

    bool neutralShapeIsStandInBox(const JPH::Shape& shape)
    {
        const JPH::Shape* current = &shape;
        while (current->GetSubType() == JPH::EShapeSubType::RotatedTranslated)
            current = static_cast<const JPH::RotatedTranslatedShape*>(current)->GetInnerShape();
        return current->GetSubType() == JPH::EShapeSubType::Box;
    }

    std::optional<NeutralNavigatorWater> neutralNavigatorWater(
        bool hasWater, bool hasHeightfield, float level, int cellSize)
    {
        if (!hasWater || !hasHeightfield || cellSize <= 0 || !std::isfinite(level))
            return std::nullopt;
        return NeutralNavigatorWater{ cellSize, level };
    }

    std::optional<float> chooseNeutralDoorFloor(float doorZ, std::span<const std::optional<float>> hits)
    {
        std::optional<float> best;
        if (!std::isfinite(doorZ))
            return best;
        for (const std::optional<float>& hit : hits)
        {
            if (!hit || !std::isfinite(*hit) || std::abs(*hit - doorZ) > kNeutralDoorFloorBand)
                continue;
            if (!best || std::abs(*hit - doorZ) < std::abs(*best - doorZ))
                best = *hit;
        }
        return best;
    }
}
