#ifndef OPENMW_COMPONENTS_JOLTHELPERS_AABB_H
#define OPENMW_COMPONENTS_JOLTHELPERS_AABB_H

#include <vsg/maths/mat4.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <components/misc/convert.hpp>

namespace PhysicsSystemHelpers
{
    inline JPH::AABox getAabb(const JPH::Shape* shape, const JPH::RMat44& transform)
    {
        const JPH::Vec3 inScale = JPH::Vec3(1.0f, 1.0f, 1.0f);
        JPH::AABox bounds = shape->GetWorldSpaceBounds(transform, inScale);
        return bounds;
    }

    inline JPH::AABox getAabb(const JPH::Shape& shape, const JPH::RMat44& transform)
    {
        return getAabb(&shape, transform);
    }

    inline JPH::AABox getAabb(const JPH::Shape& shape, const vsg::dmat4& transform)
    {
        return getAabb(&shape, Misc::Convert::toJoltNoScale(transform));
    }

    inline JPH::AABox getAabb(const JPH::Shape* shape, const vsg::dmat4& transform)
    {
        return getAabb(shape, Misc::Convert::toJoltNoScale(transform));
    }
}

#endif
