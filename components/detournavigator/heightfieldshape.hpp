#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_HEIGHFIELDSHAPE_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_HEIGHFIELDSHAPE_H

#include <components/misc/vsgmath.hpp>
#include <components/physicshelpers/heightfield.hpp>

#include <cstddef>
#include <variant>

namespace DetourNavigator
{
    struct HeightfieldPlane
    {
        float mHeight;
    };

    struct HeightfieldSurface
    {
        const float* mHeights;
        std::size_t mSize;
        float mMinHeight;
        float mMaxHeight;
    };

    using HeightfieldShape = std::variant<HeightfieldPlane, HeightfieldSurface>;

    inline vsg::vec3 getHeightfieldShift(const HeightfieldPlane& v, const vsg::ivec2& cellPosition, int cellSize)
    {
        return PhysicsSystemHelpers::getHeightfieldShift(
            cellPosition.x, cellPosition.y, cellSize, v.mHeight, v.mHeight);
    }

    inline vsg::vec3 getHeightfieldShift(const HeightfieldSurface& v, const vsg::ivec2& cellPosition, int cellSize)
    {
        return PhysicsSystemHelpers::getHeightfieldShift(
            cellPosition.x, cellPosition.y, cellSize, v.mMinHeight, v.mMaxHeight);
    }

    inline vsg::vec3 getHeightfieldShift(const HeightfieldShape& v, const vsg::ivec2& cellPosition, int cellSize)
    {
        return std::visit([&](const auto& w) { return getHeightfieldShift(w, cellPosition, cellSize); }, v);
    }
}

#endif
