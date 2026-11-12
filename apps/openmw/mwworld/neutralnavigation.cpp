#include "neutralnavigation.hpp"

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
        return kind != BaseKind::Flora && kind != BaseKind::Item && kind != BaseKind::Light;
    }

    std::optional<NeutralNavigatorWater> neutralNavigatorWater(
        bool hasWater, bool hasHeightfield, float level, int cellSize)
    {
        if (!hasWater || !hasHeightfield || cellSize <= 0 || !std::isfinite(level))
            return std::nullopt;
        return NeutralNavigatorWater{ cellSize, level };
    }
}
